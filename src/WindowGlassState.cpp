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

    if (backgroundChanged) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_samplePaddingRatio, downscale);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        int viewportWidth    = static_cast<int>(monitor->m_transformedSize.x);
        int viewportHeight   = static_cast<int>(monitor->m_transformedSize.y);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, dynamic_cast<Render::GL::CGLFramebuffer*>(source.get())->getFBID(), viewportWidth, viewportHeight);

        m_hasCachedSample     = true;
        m_lastSceneGeneration = currentGeneration;
    }

    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    // Force ARGB8888: the mask shader needs alpha precision to know where the
    // window's real surface content is, same reasoning as the layer path.
    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-window-temp");

    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, DRM_FORMAT_ARGB8888);

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

    CBox rawBox       = *windowBoxOpt;
    CBox transformBox = transformedWindowBox(rawBox, monitor);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float monitorScale  = monitor->m_scale;
    float cornerRadius  = window->rounding() * monitorScale;
    float roundingPower = window->roundingPower();

    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId      = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target         = GL_TEXTURE_2D,
        .uvOffset       = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale        = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
        .alphaThreshold = 0.001f * std::clamp(alpha, 0.0f, 1.0f),
    };

    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx,
                                     &maskInfo);
}
