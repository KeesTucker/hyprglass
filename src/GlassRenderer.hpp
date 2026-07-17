#pragma once

#include "PluginConfig.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

// Shared GL rendering pipeline used by both window decorations and layer surfaces.
// Callers own their sample framebuffers; these functions operate on passed-in state.
namespace GlassRenderer {

inline constexpr int SAMPLE_PADDING_PX = 60;

// Maximum downscale factor for blur sampling. Half-res (2) is 4x cheaper
// per blur pass. Only applied when blur is strong enough to hide the lower
// resolution — weak blur at half-res shows visible pixelation.
inline constexpr int   BLUR_DOWNSCALE_MAX       = 2;
inline constexpr float BLUR_DOWNSCALE_THRESHOLD = 0.35f; // min blur_strength for downscale

// Layers only: alpha mask from the temp FBO that captured the rendered surface.
// Constrains the glass effect to regions where the layer has visible content.
// Windows do not use masking, they pass mask=nullptr to applyGlassEffect.
struct SMaskInfo {
    GLuint   textureId;
    GLenum   target;
    Vector2D uvOffset; // mapping from glass box UV → full surface UV
    Vector2D uvScale;
    float    alphaThreshold = 0.001f;
};

// UV-space layout of a captured background sample. paddingRatio maps window
// UV to the padded texture region; validMin/validMax bound the part of the
// texture that actually received content (the blit is clamped at screen
// edges and the remainder is cleared black - the shader clamps its sampling
// to these bounds so edge windows stretch instead of refracting black).
struct SSampleLayout {
    Vector2D paddingRatio;
    Vector2D validMin = {0.0, 0.0};
    Vector2D validMax = {1.0, 1.0};
};

// sharpFramebuffer, when non-null, receives a full-resolution unblurred copy
// of the same padded region (always full-res, even when the blur sample is
// downscaled) - the shader's refracted rim samples it so the edge warp shows
// sharp content instead of invisibly displacing blur.
void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, SSampleLayout& outLayout, int downscale = 1,
                       SP<Render::IFramebuffer>* sharpFramebuffer = nullptr);

void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations,
                    GLuint callerFramebufferID, int viewportWidth, int viewportHeight);

// When mask is non-null (layers only), the shader composites the surface content
// over the glass effect in a single pass. When mask is null (windows), the shader
// outputs the glass effect alone.
// refractOutward: true = rim pulls in content from beyond the boundary
// (floating windows, layers); false = inward compression (tiled windows,
// where outward sampling would show the neighboring windows in the rim).
void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const SSampleLayout& sampleLayout, const SResolveContext& resolveContext,
                       const SMaskInfo* mask = nullptr,
                       SP<Render::IFramebuffer> sharpFramebuffer = nullptr,
                       bool refractOutward = true);

} // namespace GlassRenderer
