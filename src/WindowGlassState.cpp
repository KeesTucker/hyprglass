#include "WindowGlassState.hpp"
#include "BuiltInPresets.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

static CBox transformedWindowBox(CBox pixelBox, PHLMONITOR monitor) {
    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    pixelBox.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y).noNegativeSize().round();
    return pixelBox;
}

CWindowGlassState::CWindowGlassState(PHLWINDOW window)
    : m_window(window) {
}

CWindowGlassState::~CWindowGlassState() {
    if (g_pHyprRenderer && m_lastSize.x > 0 && m_lastSize.y > 0 &&
        std::isfinite(m_lastPosition.x) && std::isfinite(m_lastPosition.y) &&
        std::isfinite(m_lastSize.x) && std::isfinite(m_lastSize.y)) {
        auto box = CBox{m_lastPosition, m_lastSize};
        box.expand(GlassRenderer::SAMPLE_PADDING_PX).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);
    }
}

PHLWINDOW CWindowGlassState::getWindow() const {
    return m_window.lock();
}

bool CWindowGlassState::resolveEnabled() const {
    const auto& config = g_pGlobalState->config;
    const bool globalEnabled = config.enabled && **config.enabled;

    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            const auto& tags = window->m_ruleApplicator->m_tagKeeper;
            if (tags.isTagged(std::string(TAG_DISABLED)))
                return false;
            if (tags.isTagged(std::string(TAG_ENABLED)))
                return true;
        }
    } catch (...) {}

    return globalEnabled;
}

bool CWindowGlassState::resolveThemeIsDark() const {
    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            const std::string lightTag = std::string(TAG_THEME_PREFIX) + "light";
            const std::string darkTag  = std::string(TAG_THEME_PREFIX) + "dark";
            if (window->m_ruleApplicator->m_tagKeeper.isTagged(lightTag))
                return false;
            if (window->m_ruleApplicator->m_tagKeeper.isTagged(darkTag))
                return true;
        }

        const auto& config = g_pGlobalState->config;
        const auto theme = readStringConfig(config.defaultTheme);
        if (!theme.empty())
            return theme != "light";
    } catch (...) {}

    return true;
}

std::string CWindowGlassState::resolvePresetName() const {
    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            for (const auto& tag : window->m_ruleApplicator->m_tagKeeper.getTags()) {
                if (tag.starts_with(TAG_PRESET_PREFIX))
                    return tag.substr(TAG_PRESET_PREFIX.size());
            }
        }

        const auto& config = g_pGlobalState->config;
        const auto preset = readStringConfig(config.defaultPreset);
        if (!preset.empty())
            return std::string(preset);
    } catch (...) {}

    return "default";
}

void CWindowGlassState::damageIfMoved() {
    const auto window = m_window.lock();
    if (!window)
        return;

    const auto currentPosition = window->m_realPosition->value();
    const auto currentSize     = window->m_realSize->value();
    if (currentSize.x <= 0.0 || currentSize.y <= 0.0 ||
        !std::isfinite(currentPosition.x) || !std::isfinite(currentPosition.y) ||
        !std::isfinite(currentSize.x) || !std::isfinite(currentSize.y))
        return;

    const auto workspace = window->m_workspace;
    const bool isAnimating = (workspace && !window->m_pinned && workspace->m_renderOffset->isBeingAnimated()) ||
                             window->m_realPosition->isBeingAnimated() ||
                             window->m_realSize->isBeingAnimated();

    const bool moved = currentPosition != m_lastPosition || currentSize != m_lastSize;

    if (moved || isAnimating) {
        m_lastPosition = currentPosition;
        m_lastSize     = currentSize;

        auto box = CBox{currentPosition, currentSize};
        const auto monitor = window->m_monitor.lock();
        const float scale = monitor ? monitor->m_scale : 1.0f;
        box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);

        if (monitor)
            g_pGlobalState->bumpSceneGeneration(monitor.get());
    }
}

void CWindowGlassState::sampleAndRedirect(PHLMONITOR monitor, float alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!source)
        return;

    auto windowBox = WindowGeometry::computeWindowBox(window, monitor);
    if (!windowBox)
        return;

    if (alpha < 0.001f)
        return;

    CBox transformBox = transformedWindowBox(*windowBox, monitor);

    const uint64_t currentGeneration = g_pGlobalState->getSceneGeneration(monitor.get());
    const bool isAnimating = window->m_realPosition->isBeingAnimated() || window->m_realSize->isBeingAnimated();
    const bool backgroundChanged = !m_hasCachedSample ||
                                   currentGeneration != m_lastSceneGeneration ||
                                   isAnimating;

    // A generation bump from activity elsewhere doesn't mean OUR padded
    // region was re-rendered this frame: the monitor FB is persistent, and
    // outside the frame's damage it still holds last frame's *finished*
    // image - including this window's own border and surface. Resampling
    // then makes the glass refract its own previous rendering (seen live as
    // the window's own border warped into the rim). Only resample when the
    // frame's damage fully covers the padded sample region; otherwise keep
    // the cache and deliberately don't advance m_lastSceneGeneration, so we
    // retry on the next frame that actually re-renders beneath us.
    bool sampleRegionFresh = true;
    if (m_hasCachedSample) {
        CBox paddedLogical = *windowBox;
        paddedLogical.expand(GlassRenderer::SAMPLE_PADDING_PX / monitor->m_scale);
        paddedLogical = paddedLogical
                            .intersection(CBox{0.0, 0.0, monitor->m_transformedSize.x, monitor->m_transformedSize.y})
                            .noNegativeSize();
        CRegion uncovered{paddedLogical};
        uncovered.subtract(g_pHyprRenderer->m_renderData.finalDamage);
        sampleRegionFresh = uncovered.empty();
    }

    if (backgroundChanged && sampleRegionFresh) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_sampleLayout, downscale, &m_sharpFramebuffer);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        // Viewport must be restored to the monitor's *buffer* dimensions
        // (m_pixelSize), matching core's begin()/beginSimple() setViewport calls.
        // m_transformedSize has w/h swapped on 90°/270°-rotated monitors, which
        // left the viewport wrong for the entire rest of the frame.
        int viewportWidth    = static_cast<int>(monitor->m_pixelSize.x);
        int viewportHeight   = static_cast<int>(monitor->m_pixelSize.y);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, dynamic_cast<Render::GL::CGLFramebuffer*>(source.get())->getFBID(), viewportWidth, viewportHeight);

        m_hasCachedSample     = true;
        m_lastSceneGeneration = currentGeneration;
    }

    // Buffer dimensions (m_pixelSize), NOT m_transformedSize: this FBO stands in
    // for currentFB, and core allocates all monitor work buffers at m_pixelSize
    // (CMonitor::resources()). On a rotated monitor the two differ (w/h swapped)
    // and surfaces render into it with buffer-space coordinates.
    int monitorWidth  = static_cast<int>(monitor->m_pixelSize.x);
    int monitorHeight = static_cast<int>(monitor->m_pixelSize.y);

    // 16-bit float RGBA: on a 10-bit monitor (currentFormat XBGR2101010), forcing
    // ARGB8888 here quantized real window content to 8-bit on its way through this
    // redirect, even though the rest of the pipeline (blur path) stays 10-bit.
    // ABGR16161616F (GL_RGBA16F) gives full-precision RGB *and* alpha - no
    // quantization at all, unlike ABGR2101010's 2-bit alpha (4 levels), which
    // still coarsens the mask shader's unpremultiply/blend math on
    // antialiased/translucent content even though opaque content is unaffected
    // either way. Confirmed empirically (not assumed) that GL_RGBA16F is
    // color-renderable on this GPU/driver via a standalone EGL/GLES probe
    // outside Hyprland (GL_EXT_color_buffer_half_float and
    // GL_EXT_color_buffer_float both present, glTexImage2D + framebuffer
    // completeness both succeed) before relying on it here - an unsupported
    // float format would hit CGLFramebuffer::internalAlloc()'s
    // framebuffer-incomplete RASSERT, i.e. another whole-session crash.
    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-window-temp");

    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, DRM_FORMAT_ABGR16161616F);

    m_savedCurrentFB = source;

    g_pHyprRenderer->m_renderData.currentFB = m_surfaceTempFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_surfaceTempFramebuffer.get())->getFBID());

    CBox clearBox = transformBox;
    clearBox.expand(GlassRenderer::SAMPLE_PADDING_PX);
    clearBox = clearBox.intersection(CBox{0.0, 0.0, static_cast<double>(monitorWidth), static_cast<double>(monitorHeight)}).noNegativeSize().round();

    if (std::isfinite(clearBox.x) && std::isfinite(clearBox.y) && std::isfinite(clearBox.w) && std::isfinite(clearBox.h) &&
        clearBox.w > 0.0 && clearBox.h > 0.0) {
        g_pHyprOpenGL->scissor(clearBox, false);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_pHyprOpenGL->scissor(nullptr);
    }
}

void CWindowGlassState::compositeAndRestore(PHLMONITOR monitor, float alpha) {
    // Nothing was redirected (disabled, alpha too low, etc) - nothing to composite.
    if (!m_savedCurrentFB)
        return;

    g_pHyprRenderer->m_renderData.currentFB = m_savedCurrentFB;
    glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_savedCurrentFB.get())->getFBID());
    m_savedCurrentFB.reset();

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    auto target = g_pHyprRenderer->m_renderData.currentFB;
    if (!target)
        return;

    auto windowBoxOpt = WindowGeometry::computeWindowBox(window, monitor);
    if (!windowBoxOpt)
        return;

    float monitorScale = monitor->m_scale;

    // The border ring is drawn outside the window's own box (Hyprland reserves
    // border-sized decoration space around it, see CHyprBorderDecoration::draw()),
    // not inside it. Our composite quad has to reach into that ring too, or the
    // border never gets copied back from the temp FBO to the real target -
    // confirmed by reading back the temp FBO directly at the border's screen
    // position, which held valid border-colored pixels just outside rawBox.
    const float borderExpand = window->getRealBorderSize() * monitorScale;

    CBox rawBox = *windowBoxOpt;
    rawBox.expand(borderExpand);
    CBox transformBox = transformedWindowBox(rawBox, monitor);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    // Grow the rounding to stay roughly concentric with the expanded box,
    // approximating the border's own (slightly larger) outer rounding.
    float cornerRadius  = window->rounding() * monitorScale + borderExpand;
    float roundingPower = window->roundingPower();

    // Buffer dimensions, matching the temp FBO's allocation and transformBox's
    // buffer-space coordinates.
    int monitorWidth  = static_cast<int>(monitor->m_pixelSize.x);
    int monitorHeight = static_cast<int>(monitor->m_pixelSize.y);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId      = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target         = GL_TEXTURE_2D,
        .uvOffset       = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale        = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
        .alphaThreshold = 0.001f * std::clamp(alpha, 0.0f, 1.0f),
    };

    // The alpha core hands decorations is alphaValue(WINDOW_ALPHA_ACTIVE) *
    // fadeAlpha (Renderer.cpp::renderWindow's fullAlpha). Folding the
    // active/inactive opacity rule into the glass coverage let the *real*
    // unrefracted background bleed through wherever the window is
    // translucent (compA < 1) - a ghost double-image at the refracted rim,
    // where warped and true content visibly differ. A translucent window
    // should reveal the opaque glass behind it, not the raw background, so
    // divide the active/inactive factor back out and keep only fade (window
    // open/close animations must still thin the glass, or closing windows
    // would pop). Mirrors core's own opaque-rule override on renderdata.alpha.
    float activeAlpha = 1.0f;
    try {
        if (!(window->m_ruleApplicator && window->m_ruleApplicator->opaque().valueOrDefault()))
            activeAlpha = window->alphaValue(Desktop::View::WINDOW_ALPHA_ACTIVE);
    } catch (...) {}
    const float fadeOnlyAlpha = activeAlpha > 0.001f ? std::clamp(alpha / activeAlpha, 0.0f, 1.0f) : alpha;

    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, fadeOnlyAlpha,
                                     cornerRadius, roundingPower, m_sampleLayout, ctx,
                                     &maskInfo, m_sharpFramebuffer,
                                     /* refractOutward = */ window->m_isFloating);
}
