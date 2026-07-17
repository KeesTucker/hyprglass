#include "GlassCompositePassElement.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"
#include "WindowGlassState.hpp"

CGlassCompositePassElement::CGlassCompositePassElement(const SGlassCompositeData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassCompositePassElement::draw() {
    if (m_data.windowState && m_data.windowState->getWindow())
        m_data.windowState->compositeAndRestore(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);

    return {};
}

std::optional<CBox> CGlassCompositePassElement::boundingBox() {
    if (!m_data.windowState)
        return std::nullopt;

    auto window = m_data.windowState->getWindow();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = WindowGeometry::computeWindowBox(window, monitor);
    if (!box)
        return std::nullopt;

    const float padding = GlassRenderer::SAMPLE_PADDING_PX / monitor->m_scale;
    box->expand(padding);
    return box;
}

bool CGlassCompositePassElement::needsLiveBlur() {
    return false;
}

bool CGlassCompositePassElement::needsPrecomputeBlur() {
    return false;
}
