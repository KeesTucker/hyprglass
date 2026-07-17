#include "GlassLayerSurface.hpp"
#include "BuiltInPresets.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <hyprland/src/desktop/Workspace.hpp>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

static CBox transformedLayerBox(CBox pixelBox, PHLMONITOR monitor) {
    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    pixelBox.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y).noNegativeSize().round();
    return pixelBox;
}

CGlassLayerSurface::CGlassLayerSurface(PHLLS layerSurface)
    : m_layerSurface(layerSurface) {
}

CGlassLayerSurface::~CGlassLayerSurface() {
    // Damage the area where glass was last drawn so the compositor
    // re-renders it without the glass effect (prevents ghost artifacts).
    if (g_pHyprRenderer && m_lastSize.x > 0 && m_lastSize.y > 0 &&
        std::isfinite(m_lastPosition.x) && std::isfinite(m_lastPosition.y) &&
        std::isfinite(m_lastSize.x) && std::isfinite(m_lastSize.y)) {
        auto box = CBox{m_lastPosition, m_lastSize};
        box.expand(GlassRenderer::SAMPLE_PADDING_PX).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);
    }
}

bool CGlassLayerSurface::resolveThemeIsDark() const {
    try {
        const auto& config = g_pGlobalState->config;
        const auto theme = readStringConfig(config.defaultTheme);
        if (!theme.empty())
            return theme != "light";
    } catch (...) {}

    return true;
}

std::string CGlassLayerSurface::resolvePresetName() const {
    try {
        // Per-namespace preset override (highest priority)
        const auto layerSurface = m_layerSurface.lock();
        if (layerSurface) {
            const auto& nsPresets = g_pGlobalState->layerNamespacePresets;
            auto it = nsPresets.find(layerSurface->m_namespace);
            if (it != nsPresets.end())
                return it->second;
        }

        const auto& config = g_pGlobalState->config;

        // Layer-wide preset override
        const auto layerPreset = readStringConfig(config.layersPreset);
        if (!layerPreset.empty())
            return std::string(layerPreset);

        // Fall back to global default preset
        const auto defaultPreset = readStringConfig(config.defaultPreset);
        if (!defaultPreset.empty())
            return std::string(defaultPreset);
    } catch (...) {}

    return "default";
}

PHLLS CGlassLayerSurface::getLayerSurface() const {
    return m_layerSurface.lock();
}

void CGlassLayerSurface::damageIfMoved() {
    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    const auto currentPosition = layerSurface->m_realPosition->value();
    const auto currentSize     = layerSurface->m_realSize->value();
    if (currentSize.x <= 0.0 || currentSize.y <= 0.0 ||
        !std::isfinite(currentPosition.x) || !std::isfinite(currentPosition.y) ||
        !std::isfinite(currentSize.x) || !std::isfinite(currentSize.y))
        return;

    const bool isAnimating = layerSurface->m_realPosition->isBeingAnimated() ||
                             layerSurface->m_realSize->isBeingAnimated() ||
                             layerSurface->m_alpha->isBeingAnimated() ||
                             layerSurface->m_fadingOut;

    const bool moved = currentPosition != m_lastPosition || currentSize != m_lastSize;

    if (moved || isAnimating) {
        m_lastPosition  = currentPosition;
        m_lastSize      = currentSize;

        auto box = CBox{currentPosition, currentSize};
        const auto monitor = layerSurface->m_monitor.lock();
        const float scale = monitor ? monitor->m_scale : 1.0f;
        box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);

        if (monitor)
            g_pGlobalState->bumpSceneGeneration(monitor.get());
    }
}

void CGlassLayerSurface::sampleAndRedirect(PHLMONITOR monitor, float alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!source)
        return;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox transformBox = transformedLayerBox(*layerBox, monitor);

    // Same instant-update scheme as the window path: the sharp FBO is the
    // authoritative unblurred background cache - full recapture on geometry
    // change, per-damage-rect updates otherwise (never reading stale FB
    // areas, which contain this layer's own previous output).
    CBox paddedBuffer = transformBox;
    paddedBuffer.expand(GlassRenderer::SAMPLE_PADDING_PX);
    paddedBuffer = paddedBuffer
                       .intersection(CBox{0.0, 0.0, monitor->m_pixelSize.x, monitor->m_pixelSize.y})
                       .noNegativeSize();

    CRegion bufferDamage = g_pHyprRenderer->m_renderData.finalDamage.copy();
    bufferDamage.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)),
                           monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    bufferDamage.intersect(paddedBuffer);

    const bool geometryChanged = !m_hasCachedSample || transformBox != m_lastSampleBox;

    if (layerSurface->m_fadingOut) {
        // During fade-out, re-sampling captures stale pixels. Reuse cached sample.
        if (!m_hasCachedSample)
            return;
    } else if (geometryChanged || !bufferDamage.empty()) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_sampleLayout, downscale,
                                        &m_sharpFramebuffer, geometryChanged ? nullptr : &bufferDamage);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        // Buffer dimensions (m_pixelSize), matching core's setViewport at render
        // begin - m_transformedSize is w/h-swapped on rotated monitors.
        int viewportWidth    = static_cast<int>(g_pHyprRenderer->m_renderData.pMonitor->m_pixelSize.x);
        int viewportHeight   = static_cast<int>(g_pHyprRenderer->m_renderData.pMonitor->m_pixelSize.y);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, dynamic_cast<Render::GL::CGLFramebuffer*>(source.get())->getFBID(), viewportWidth, viewportHeight);

        m_hasCachedSample = true;
        m_lastSampleBox   = transformBox;
    }
    // else: nothing re-rendered under us this frame, reuse cached blur

    // Redirect surface rendering to a temp FBO cleared to transparent.
    // The original renderLayer (called between pre/post elements) will render
    // the surface into this FBO. compositeAndRestore uses its alpha as a mask.
    // Buffer dimensions (m_pixelSize), NOT m_transformedSize: this FBO stands in
    // for currentFB, which core allocates at m_pixelSize. The two differ (w/h
    // swapped) on rotated monitors.
    int monitorWidth  = static_cast<int>(monitor->m_pixelSize.x);
    int monitorHeight = static_cast<int>(monitor->m_pixelSize.y);

    // 16-bit float RGBA for the temp FBO: the mask shader needs alpha precision,
    // and monitor FBOs use XRGB formats (no usable alpha). ARGB8888 used to be
    // forced here, quantizing content to 8-bit even on a 10-bit monitor.
    // ABGR2101010 (10-bit RGB) was considered and rejected for this path
    // specifically: its 2-bit alpha (4 levels) quantizes values below ~0.17 to
    // zero, breaking the mask discard for low-opacity layers - layers commonly
    // fade their own alpha (see isAnimating's m_alpha->isBeingAnimated() check
    // above), so a fading layer would pop to fully invisible partway through
    // instead of smoothly fading. ABGR16161616F (GL_RGBA16F) avoids that
    // entirely - full-precision alpha, no quantization at any opacity level.
    // Confirmed empirically (not assumed) that GL_RGBA16F is color-renderable
    // on this GPU/driver via a standalone EGL/GLES probe outside Hyprland
    // (GL_EXT_color_buffer_half_float and GL_EXT_color_buffer_float both
    // present, glTexImage2D + framebuffer completeness both succeed) before
    // relying on it here - an unsupported float format would hit
    // CGLFramebuffer::internalAlloc()'s framebuffer-incomplete RASSERT, i.e.
    // another whole-session crash.
    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-layer-temp");

    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, DRM_FORMAT_ABGR16161616F);

    // Mirror the real target's image description: core code running during
    // the redirect reads currentFB->imageDescription() (e.g. the shadow
    // renderer's CM path) and an unset one aborts the compositor. See the
    // matching comment in WindowGlassState::sampleAndRedirect.
    m_surfaceTempFramebuffer->setImageDescription(source->imageDescription());

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

void CGlassLayerSurface::compositeAndRestore(PHLMONITOR monitor, float alpha) {
    // Restore the original currentFB before compositing
    if (m_savedCurrentFB) {
        g_pHyprRenderer->m_renderData.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_savedCurrentFB.get())->getFBID());
        m_savedCurrentFB.reset();
    }

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto target = g_pHyprRenderer->m_renderData.currentFB;
    if (!target)
        return;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox rawBox       = *layerBox;
    CBox transformBox = transformedLayerBox(rawBox, monitor);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float cornerRadius  = 0.0f;
    float roundingPower = 2.0f;

    // Use the temp FBO's rendered alpha as a mask: glass only where the surface
    // has visible content (alpha > 0). The temp FBO is in monitor coordinates,
    // so we map from the glass quad UV to monitor UV.
    // Buffer dimensions, matching the temp FBO's allocation and transformBox's
    // buffer-space coordinates.
    int monitorWidth  = static_cast<int>(monitor->m_pixelSize.x);
    int monitorHeight = static_cast<int>(monitor->m_pixelSize.y);

    float maskThreshold = 0.001f;
    auto threshIt = g_pGlobalState->layerNamespaceMaskThresholds.find(layerSurface->m_namespace);
    if (threshIt != g_pGlobalState->layerNamespaceMaskThresholds.end())
        maskThreshold = threshIt->second;

    // The temp FBO stores the layer after Hyprland applies fade alpha. Keep
    // mask_threshold relative to the layer's content alpha, otherwise fade-out
    // makes the mask fall below threshold early and the glass blinks off.
    maskThreshold *= std::clamp(alpha, 0.0f, 1.0f);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId         = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target            = GL_TEXTURE_2D,
        .uvOffset          = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale           = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
        .alphaThreshold    = maskThreshold,
    };

    // The glass shader composites both the glass effect and the surface content
    // in a single pass: glass behind, surface on top, using the temp FBO alpha.
    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_sampleLayout, ctx,
                                     &maskInfo, m_sharpFramebuffer,
                                     /* refractOutward = */ true);
}
