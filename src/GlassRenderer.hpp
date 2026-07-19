#pragma once

#include "PluginConfig.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <optional>

// Shared GL rendering pipeline used by both window decorations and layer surfaces.
// Callers own their sample framebuffers; these functions operate on passed-in state.
namespace GlassRenderer {

inline constexpr int SAMPLE_PADDING_PX = 60;

// Maximum downscale factor for blur sampling. Half-res (2) is 4x cheaper
// per blur pass. Only applied when blur is strong enough to hide the lower
// resolution — weak blur at half-res shows visible pixelation.
inline constexpr int   BLUR_DOWNSCALE_MAX       = 2;
inline constexpr float BLUR_DOWNSCALE_THRESHOLD = 0.35f; // min blur_strength for downscale

// Side length (px) of the offscreen buffer used by computeAlphaContentBox to
// detect a layer's real content bounds. Coarse on purpose: the searched
// region is downsampled GPU-side to this size before the one CPU readback, so
// cost is independent of how large the searched box is - a finer probe only
// tightens the detected box, it doesn't change the cost model.
inline constexpr int ALPHA_PROBE_SIZE = 128;

// Jump Flood Algorithm distance field, layers only. Buffer sizing is aspect-
// preserving - a squashed aspect would distort the gradient direction used
// for refractionDir - so one scale factor derived from the content box's
// longer dimension is applied to both axes before independently clamping
// each to [JFA_MIN_DIM, JFA_MAX_DIM].
inline constexpr int JFA_DOWNSCALE = 1;   // content-box px -> field texels
inline constexpr int JFA_MAX_DIM   = 512; // cap on the buffer's longer side;
                                           // cost-control only (JFA passes
                                           // are still tiny next to the
                                           // existing multi-iteration blur).
inline constexpr int JFA_MIN_DIM = 8;

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

// UV-space layout of a captured background sample. uvOffset/uvScale map a
// box's local UV [0,1] to the padded texture's UV (texUV = uv * uvScale +
// uvOffset); for the box a sample was captured for, this simply carves out
// the non-padded interior. narrowSampleLayout() remaps these to instead
// address a tighter sub-rect of that same captured texture. validMin/validMax
// bound the part of the texture that actually received content (the blit is
// clamped at screen edges and the remainder is cleared black - the shader
// clamps its sampling to these bounds so edge windows stretch instead of
// refracting black); they're already absolute texture-space bounds, so they
// don't change when narrowing to a sub-rect.
struct SSampleLayout {
    Vector2D uvOffset = {0.0, 0.0};
    Vector2D uvScale  = {1.0, 1.0};
    Vector2D validMin = {0.0, 0.0};
    Vector2D validMax = {1.0, 1.0};
};

// Layers only: per-pixel unsigned distance-to-boundary field of a layer's
// alpha silhouette, computed via Jump Flooding. Ping-ponged RGBA16F buffer
// pair (DRM_FORMAT_ABGR16161616F, same format already proven color-
// renderable on this GPU/driver by m_surfaceTempFramebuffer - see the format
// comment in GlassLayerSurface::sampleAndRedirect) storing each texel's
// current best-known nearest-boundary seed as (seedX, seedY, validFlag) in
// .rgb - float storage means exact seed positions with no encoding/precision
// games, and floats are never subject to the sRGB transfer function that
// makes raw UNORM8 RGB channels risky to trust for exact values elsewhere in
// this codebase. bufA/bufB are forced to GL_NEAREST (required during
// propagation - bilinearly blending two unrelated seed *positions* together
// corrupts them), so a finalize pass bakes the actual per-texel distance
// (not raw positions) into bufFinal, which is left at the default
// GL_LINEAR - hardware-interpolating a real distance value is meaningful in
// a way interpolating raw seed coordinates isn't, and this is what
// liquidglass.frag samples. Consumed there to replace the closed-form box
// SDF for non-rectangular content (e.g. waybar's separate pill-shaped
// module groups). Owned per CGlassLayerSurface instance, not shared, so
// multiple simultaneous layer-glass surfaces don't thrash a shared buffer's
// size every frame.
struct SDistanceFieldBuffers {
    SP<Render::IFramebuffer> bufA, bufB, bufFinal;
};

struct SDistanceFieldResult {
    GLuint   texId = 0;
    Vector2D fieldSize; // resolved field width/height, in field texels
};

// Runs the JFA seed + propagation passes over buffers sized from
// contentBox. Returns std::nullopt when contentBox is degenerate - callers
// should fall back to the closed-form box SDF, same convention as
// computeAlphaContentBox's std::nullopt fallback.
std::optional<SDistanceFieldResult> computeDistanceField(SDistanceFieldBuffers& buffers, SP<Render::IFramebuffer> maskSource,
                                                           const SMaskInfo& mask, CBox contentBox,
                                                           GLuint callerFramebufferID, int viewportWidth, int viewportHeight);

// sharpFramebuffer, when non-null, receives a full-resolution unblurred copy
// of the same padded region (always full-res, even when the blur sample is
// downscaled) - the shader's refracted rim samples it, and it doubles as the
// authoritative background cache: pass partialDamage (buffer coords,
// pre-intersected with the padded region) to update only freshly-rendered
// rects instead of recapturing everything; nullptr = full capture (required
// whenever the sampled geometry changed).
void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, SSampleLayout& outLayout, int downscale = 1,
                       SP<Render::IFramebuffer>* sharpFramebuffer = nullptr,
                       const CRegion* partialDamage = nullptr);

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
                       bool refractOutward = true,
                       const SDistanceFieldResult* distField = nullptr);

// Layers only: tight axis-aligned bounding box (in the same buffer/pixel
// space as searchBox) of alphaSource's non-transparent content within
// searchBox. Used so refraction geometry hugs a layer's actual visible
// content instead of its reported (possibly oversized) box - e.g. a launcher
// that reports fullscreen bounds for click-catching but only draws a small
// centered panel. Downsamples to a small probe FBO (ALPHA_PROBE_SIZE) before
// a single CPU readback, so this stays cheap regardless of searchBox's size.
// Returns std::nullopt when nothing exceeds alphaThreshold (not yet rendered,
// or fully hidden this frame) - callers should fall back to searchBox itself.
std::optional<CBox> computeAlphaContentBox(SP<Render::IFramebuffer>& probeFramebuffer,
                                            SP<Render::IFramebuffer> alphaSource,
                                            CBox searchBox, float alphaThreshold);

// Remaps a sample layout captured for capturedBox so its UVs instead address
// contentBox, a sub-rect of it in the same buffer/pixel space. validMin/Max
// are already absolute texture-space bounds and pass through unchanged.
SSampleLayout narrowSampleLayout(const SSampleLayout& base, const CBox& capturedBox, const CBox& contentBox);

} // namespace GlassRenderer
