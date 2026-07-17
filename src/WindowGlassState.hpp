#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

// Per-window glass state shared between the BOTTOM decoration (redirect) and
// the OVER decoration (composite), mirroring CGlassLayerSurface's role for
// layer surfaces but bracketing a window's own CSurfacePassElement instead
// of a hooked renderLayer call.
class CWindowGlassState {
  public:
    explicit CWindowGlassState(PHLWINDOW window);
    ~CWindowGlassState();

    // Pre-surface (BOTTOM layer): sample+blur background, redirect currentFB -> temp FBO
    void sampleAndRedirect(PHLMONITOR monitor, float alpha);

    // Post-surface (OVER layer): restore currentFB, composite glass masked by temp FBO
    void compositeAndRestore(PHLMONITOR monitor, float alpha);

    void damageIfMoved();

    [[nodiscard]] PHLWINDOW getWindow() const;
    [[nodiscard]] bool      resolveEnabled() const;

  private:
    PHLWINDOWREF              m_window;
    SP<Render::IFramebuffer>  m_sampleFramebuffer;
    SP<Render::IFramebuffer>  m_surfaceTempFramebuffer;
    Vector2D                  m_samplePaddingRatio;
    bool                      m_hasCachedSample = false;

    Vector2D m_lastPosition;
    Vector2D m_lastSize;

    uint64_t m_lastSceneGeneration = 0;

    SP<Render::IFramebuffer> m_savedCurrentFB;

    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;
};
