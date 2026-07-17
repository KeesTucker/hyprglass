#include "GlassDecoration.hpp"
#include "GlassPassElement.hpp"
#include "Globals.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>

CGlassDecoration::CGlassDecoration(PHLWINDOW window, std::shared_ptr<CWindowGlassState> state)
    : IHyprWindowDecoration(window), m_window(window), m_state(std::move(state)) {
}

SDecorationPositioningInfo CGlassDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.priority       = 10000;
    info.policy         = DECORATION_POSITION_ABSOLUTE;
    info.desiredExtents = {{0, 0}, {0, 0}};
    return info;
}

void CGlassDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {}

void CGlassDecoration::draw(PHLMONITOR monitor, float const& alpha) {
    if (!g_pGlobalState || !m_state || !m_state->resolveEnabled())
        return;

    CGlassPassElement::SGlassPassData data{m_state, alpha};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassPassElement>(data));

    const auto window = m_window.lock();
    if (window) {
        const auto workspace = window->m_workspace;

        const bool wsAnimating = workspace && !window->m_pinned && workspace->m_renderOffset->isBeingAnimated();
        if (wsAnimating)
            damageEntire();

        const auto currentPosition = window->m_realPosition->value();
        const auto currentSize = window->m_realSize->value();
        const bool moved = currentPosition != m_lastPosition || currentSize != m_lastSize;
        if (moved) {
            damageEntire();
            m_lastPosition = currentPosition;
            m_lastSize = currentSize;
        }

        if (moved || wsAnimating) {
            if (auto mon = window->m_monitor.lock())
                g_pGlobalState->bumpSceneGeneration(mon.get());
        }
    }
}

eDecorationType CGlassDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CGlassDecoration::updateWindow(PHLWINDOW window) {
    damageEntire();
}

void CGlassDecoration::damageEntire() {
    const auto window = m_window.lock();
    if (!window)
        return;

    const auto workspace = window->m_workspace;
    auto surfaceBox = window->getWindowMainSurfaceBox();

    if (workspace && workspace->m_renderOffset->isBeingAnimated() && !window->m_pinned)
        surfaceBox.translate(workspace->m_renderOffset->value());
    surfaceBox.translate(window->m_floatingOffset);

    const auto monitor = window->m_monitor.lock();
    const float scale = monitor ? monitor->m_scale : 1.0f;
    surfaceBox.expand(GlassRenderer::SAMPLE_PADDING_PX / scale);

    g_pHyprRenderer->damageBox(surfaceBox);
}

eDecorationLayer CGlassDecoration::getDecorationLayer() {
    return DECORATION_LAYER_BOTTOM;
}

uint64_t CGlassDecoration::getDecorationFlags() {
    return 0;
}

std::string CGlassDecoration::getDisplayName() {
    return "HyprGlass";
}
