#include "GlassCompositeDecoration.hpp"
#include "GlassCompositePassElement.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>

CGlassCompositeDecoration::CGlassCompositeDecoration(PHLWINDOW window, std::shared_ptr<CWindowGlassState> state)
    : IHyprWindowDecoration(window), m_window(window), m_state(std::move(state)) {
}

SDecorationPositioningInfo CGlassCompositeDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.priority       = 10000;
    info.policy         = DECORATION_POSITION_ABSOLUTE;
    info.desiredExtents = {{0, 0}, {0, 0}};
    return info;
}

void CGlassCompositeDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {}

void CGlassCompositeDecoration::draw(PHLMONITOR monitor, float const& alpha) {
    if (!g_pGlobalState || !m_state || !m_state->resolveEnabled())
        return;

    CGlassCompositePassElement::SGlassCompositeData data{m_state, alpha};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassCompositePassElement>(data));
}

eDecorationType CGlassCompositeDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CGlassCompositeDecoration::updateWindow(PHLWINDOW window) {
    damageEntire();
}

void CGlassCompositeDecoration::damageEntire() {
    const auto window = m_window.lock();
    if (!window)
        return;

    auto surfaceBox = window->getWindowMainSurfaceBox();
    const auto workspace = window->m_workspace;
    if (workspace && workspace->m_renderOffset->isBeingAnimated() && !window->m_pinned)
        surfaceBox.translate(workspace->m_renderOffset->value());
    surfaceBox.translate(window->m_floatingOffset);

    const auto monitor = window->m_monitor.lock();
    const float scale = monitor ? monitor->m_scale : 1.0f;
    surfaceBox.expand(GlassRenderer::SAMPLE_PADDING_PX / scale);

    g_pHyprRenderer->damageBox(surfaceBox);
}

eDecorationLayer CGlassCompositeDecoration::getDecorationLayer() {
    return DECORATION_LAYER_OVER;
}

uint64_t CGlassCompositeDecoration::getDecorationFlags() {
    return 0;
}

std::string CGlassCompositeDecoration::getDisplayName() {
    return "HyprGlassComposite";
}
