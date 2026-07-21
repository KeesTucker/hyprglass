// Auto-generated shader header - Do not edit!
#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader — Thick-glass refraction model
 *
 * The window is modeled as a thick convex glass slab:
 *   - Center: flat surface → clean frosted blur, no distortion
 *   - Edges: curved surface → refraction pulls in content from beyond
 *     the window boundary, creating natural color bleeding
 *
 * Rendering layers:
 * 1. Edge refraction: rim samples *outward*, pulling in sharp content from
 *    beyond the window boundary (via the padded sharp sample texture),
 *    compressed by the exponential falloff - visible warping, not blurred mush
 * 2. Chromatic aberration (per-channel refraction scale, on the sharp rim)
 * 3. Edge raw-texture blend for vivid color pickup
 * 4. Subtle center dome lens magnification
 * 5. Frosted tint (brightness boost + desaturation)
 * 6. Configurable color tint overlay
 * 7. Fresnel edge glow
 * 8. Specular highlight (top)
 * 9. Inner shadow (bottom rim)
 */

uniform sampler2D tex;      // blurred padded background sample
uniform sampler2D sharpTex; // unblurred full-res copy of the same region
uniform vec4 validBounds;   // xy=min, zw=max valid UV of the captured region:
                            // near screen edges the capture is clamped and the
                            // rest of the FBO is cleared black - clamping the
                            // sample UV here stretches edge content instead of
                            // refracting black
uniform float refractionDirSign; // +1: rim samples outward (floating windows,
                                 // layers - pulls content from beyond the edge)
                                 // -1: inward compression (tiled windows -
                                 // outward would pull in neighboring windows)
uniform vec2 fullSize;
uniform float radius;
uniform vec2 uvOffset; // maps this box's local UV [0,1] to the padded sample texture's UV
uniform vec2 uvScale;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float vibrancy;
uniform float vibrancyDarkness;
uniform float adaptiveDim;
uniform float adaptiveBoost;
uniform float roundingPower;

uniform sampler2D maskTex;
uniform int useMask;
uniform vec2 maskUVOffset;
uniform vec2 maskUVScale;
uniform float maskAlphaThreshold;

// Windows only: how strongly the window's own opaque content fades out at
// the refraction band, revealing glass, before fading back to fully opaque
// toward the interior - see the hasMask composite step in main(). Already
// has the focus-ease amount and configured strength folded in on the CPU
// side (WindowGlassState.cpp), so 0.0 here (layers, or an unfocused
// window) is a complete no-op.
uniform float edgeOpacityFade;

// Layers only: per-pixel distance-to-boundary field replacing the
// closed-form box SDF below, so refraction hugs the layer's actual alpha
// silhouette (e.g. multiple separate pill-shaped bar modules) instead of a
// single rounded rectangle. See getDistanceFieldSDF()/refractionDirField().
uniform sampler2D distField;
uniform vec2 distFieldSize;
uniform int useDistanceField;
uniform float gradientStepTexels; // see refractionDirField()

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

vec2 toTexUV(vec2 wuv) {
    return wuv * uvScale + uvOffset;
}

vec2 clampValid(vec2 tuv) {
    return clamp(tuv, validBounds.xy + 0.001, validBounds.zw - 0.001);
}

vec4 sampleBlurred(vec2 wuv) {
    return texture(tex, clampValid(toTexUV(wuv)));
}

vec4 sampleSharp(vec2 wuv) {
    return texture(sharpTex, clampValid(toTexUV(wuv)));
}

// ============================================================================
// SDF
// ============================================================================

float lpNorm(vec2 v, float p) {
    return pow(pow(abs(v.x), p) + pow(abs(v.y), p), 1.0 / p);
}

float getRoundedBoxSDF(vec2 uv, float r) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float clampedR = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + clampedR;
    return min(max(q.x, q.y), 0.0) + lpNorm(max(q, 0.0), roundingPower) - clampedR;
}

float getCornerSDF(vec2 uv) {
    return getRoundedBoxSDF(uv, radius);
}

// ============================================================================
// DISTANCE FIELD SDF (layers only, non-rectangular content)
// distField holds a *signed* distance-to-boundary in real pixels, baked by
// jfafinalize.frag from the Jump Flooding passes' nearest-boundary seeds:
// positive inside the mask silhouette, negative outside. The mask alpha
// discard earlier in main() already gates which fragments get shaded, but
// the sign is still load-bearing for refractionDirField(): its gradient
// stencil reaches several texels across the boundary, and an unsigned
// field folds into a V there (both sides positive), collapsing/flipping
// the gradient wherever a stencil arm lands outside - which concentrated
// at corners as visible diagonal seams.
// ============================================================================

// distField already holds the baked, hardware-bilinear-filterable distance
// value (see jfafinalize.frag) - not raw seed positions, which must stay
// nearest-sampled during the JFA passes themselves (see
// SDistanceFieldBuffers's comment). A direct filtered fetch here is what
// makes curved silhouettes (e.g. a circle) look smooth instead of
// staircased at the field's native texel resolution.
float getDistanceFieldSDF(vec2 uv) {
    return texture(distField, uv).r;
}

// ============================================================================
// REFRACTION DIRECTION
// Pixel-space direction toward window center — perfectly smooth everywhere,
// no SDF gradient needed. On straight edges the perpendicular pixel distance
// dominates, giving approximately edge-normal direction. At corners it
// naturally follows the diagonal.
// ============================================================================

vec2 refractionDir(vec2 uv) {
    vec2 toCenterPx = (vec2(0.5) - uv) * fullSize;
    float len = length(toCenterPx);
    return len > 0.1 ? toCenterPx / len : vec2(0.0);
}

// Distance-field equivalent of refractionDir, for non-rectangular layer
// content. getDistanceFieldSDF is signed (positive inside) and increases
// moving from outside through the boundary into the interior, so its
// gradient already points "further inside" - the same direction
// refractionDir's toCenterPx approximates for a single convex rectangle -
// no sign flip needed, and stencil arms crossing the boundary stay on the
// same monotone ramp. Same near-singularity guard as refractionDir's
// dead-center case.
//
// The finite-difference step is deliberately several field texels wide
// (GRADIENT_STEP_TEXELS), not one: bilinear filtering in getDistanceFieldSDF
// smooths the *value* between texels, but a one-texel-wide difference still
// differentiates the field at its native JFA grid resolution, so the
// resulting direction snaps at each texel boundary - visible as a choppy,
// faceted refraction pattern even where the field's distance value looks
// smooth. Widening the step low-pass-filters that quantization out of the
// gradient at the cost of a little directional sharpness right at concave
// corners.
//
// It also does double duty smoothing the seam between separate silhouette
// lobes (e.g. two of a bar's pill-shaped module groups sitting close
// together, or the narrow waist of a dumbbell-shaped mask): each lobe's
// field points toward its own nearest boundary, so right where two lobes
// meet or nearly meet, the direction flips rather than blending - a visible
// hard crease between what look like two independent glass bulges. A wider
// step averages samples from across that flip instead of straddling it by
// a texel or two, turning the crease into a gradual blend. layers:
// distance_field_resolution / the fixed JFA_DOWNSCALE (GlassRenderer.hpp)
// put the field close to 1:1 with real pixels for most layers, so this is
// roughly a real-pixel blend radius. gradientStepTexels itself is exposed
// as layers:refraction_blend.
vec2 refractionDirField(vec2 uv) {
    vec2 texel = (gradientStepTexels / distFieldSize);
    float dxp = getDistanceFieldSDF(uv + vec2(texel.x, 0.0));
    float dxn = getDistanceFieldSDF(uv - vec2(texel.x, 0.0));
    float dyp = getDistanceFieldSDF(uv + vec2(0.0, texel.y));
    float dyn = getDistanceFieldSDF(uv - vec2(0.0, texel.y));
    vec2 grad = vec2(dxp - dxn, dyp - dyn);
    float len = length(grad);
    return len > 0.001 ? grad / len : vec2(0.0);
}

// ============================================================================
// MAIN — Thick-glass refraction model
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    // Layers only: sample the temp FBO to get the rendered surface pixel.
    // Discard fully transparent fragments so glass only covers visible content.
    // For windows, hasMask is false and this block is skipped entirely.
    vec4 surfacePixel = vec4(0.0);
    bool hasMask = (useMask == 1);
    if (hasMask) {
        vec2 maskUV = uv * maskUVScale + maskUVOffset;
        surfacePixel = texture(maskTex, clamp(maskUV, 0.001, 0.999));
        if (surfacePixel.a < maskAlphaThreshold) discard;
    }

    float cornerSdf = useDistanceField == 1 ? -getDistanceFieldSDF(uv) : getCornerSDF(uv);

    // Layers (hasMask): never discard on the SDF. The mask alpha discard
    // above already shapes the silhouette at full mask resolution, the
    // distance field is coarser than the mask, and this same fragment must
    // still composite the surface pixel even where the glass fades to
    // nothing - cornerAlpha zeroing glassA handles that. Windows have no
    // mask, so the SDF discard is their only shaping.
    if (!hasMask && cornerSdf > 0.0) {
        discard;
    }

    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    if (!hasMask && cornerAlpha < 0.001) discard;

    float minDim = min(fullSize.x, fullSize.y);

    // Fixed reference, not minDim: bezel width in real pixels used to be
    // edgeThickness * minDim, so the same edge_thickness value gave a huge
    // window a thick bezel and a small popup/notification a paper-thin one.
    // Multiplying by a constant instead makes the refraction band the same
    // absolute size on every window/layer regardless of its own dimensions.
    const float EDGE_THICKNESS_REFERENCE_PX = 1000.0;
    // ...but clamped to a fraction of the box's own minDim, so a box smaller
    // than the fixed band (a small popup, notification, or a layer whose
    // real alpha content is tiny - fullSize here is already the tightened
    // alpha content box, not the reported box) still keeps a flat, readable
    // interior instead of the bezel swallowing the whole thing and going
    // wild edge-to-edge.
    const float MAX_BEZEL_FRACTION_OF_BOX = 0.30;
    float bezelWidthPx = min(edgeThickness * EDGE_THICKNESS_REFERENCE_PX, minDim * MAX_BEZEL_FRACTION_OF_BOX);

    // ========================================
    // EDGE PROXIMITY + DIRECTION
    // edgeProximity: 1.0 at boundary, exponential decay inward
    // inwardDir: pixel-space direction toward center (smooth everywhere)
    // ========================================
    // min(): a layer fragment can sit slightly *outside* the signed field's
    // zero crossing (antialiased mask fringe, field coarseness) while still
    // passing the mask alpha discard - without the clamp, exp() of a
    // positive sdf would amplify refraction/fresnel there beyond the
    // boundary maximum instead of saturating at it.
    float edgeProximity = exp(min(cornerSdf, 0.0) / bezelWidthPx);
    vec2 inwardDir = useDistanceField == 1 ? refractionDirField(uv) : refractionDir(uv);

    // ========================================
    // EDGE REFRACTION
    // Offset sampling UV *outward* at edges — the curved bezel of a thick
    // glass slab bends rays so the rim shows content from beyond the
    // window boundary (reachable thanks to the sample's padding region),
    // compressed by the exponential falloff: fragments nearer the edge
    // reach farther out, so the displacement gradient squeezes the outside
    // world into the rim band.
    // ========================================
    float refractionPx = refractionStrength * 50.0;
    float refractionMag = edgeProximity * refractionPx;
    vec2 baseOffset = -inwardDir * refractionDirSign * refractionMag / fullSize;

    // ========================================
    // CHROMATIC ABERRATION — per-channel refraction scale
    // Blue refracts more than red → natural spectral fringing at edges.
    // ========================================
    float chromaSpread = chromaticAberration * 0.35;
    vec2 offsetR = baseOffset * (1.0 - chromaSpread);
    vec2 offsetG = baseOffset;
    vec2 offsetB = baseOffset * (1.0 + chromaSpread);

    // ========================================
    // CENTER DOME LENS (subtle magnification in the flat interior)
    // Fades near edges so it doesn't interfere with edge refraction.
    // ========================================
    vec2 domeUV = vec2(0.0);
    if (lensDistortion > 0.001) {
        vec2 c = (uv - 0.5) * 2.0;
        vec2 dGrad = vec2(
            -4.0 * c.x * (1.0 - c.y * c.y),
            -4.0 * c.y * (1.0 - c.x * c.x)
        );
        float lensMaxPx = lensDistortion * minDim * 0.006;
        float lensFade = 1.0 - edgeProximity;
        domeUV = dGrad * lensMaxPx * lensFade / fullSize;
    }

    // ========================================
    // BACKGROUND SAMPLING (frosted blur only)
    // Nearby color influence comes naturally from the Gaussian blur
    // kernel crossing the window boundary — no explicit raw sampling.
    // ========================================
    vec3 color;
    vec2 uvR = uv + offsetR + domeUV;
    vec2 uvG = uv + offsetG + domeUV;
    vec2 uvB = uv + offsetB + domeUV;

    if (chromaticAberration > 0.001 && edgeProximity > 0.01) {
        color.r = sampleBlurred(uvR).r;
        color.g = sampleBlurred(uvG).g;
        color.b = sampleBlurred(uvB).b;
    } else {
        color = sampleBlurred(uvG).rgb;
    }

    // ========================================
    // FROSTED TINT (per-theme tone mapping)
    // ========================================
    float blurredLum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Frosted desaturation
    color = mix(vec3(blurredLum), color, saturation);

    // Tight smoothstep range maps the blur-compressed luminance (~0.3-0.7)
    // to the full [0,1] adaptive range, creating visible per-region differentiation
    float lumCurve = smoothstep(0.25, 0.55, blurredLum);

    // Dim: multiplicative — effective at darkening bright areas
    color *= brightness * (1.0 - adaptiveDim * lumCurve);

    // Boost: additive lift — multiplicative can't brighten near-black content
    color += vec3(adaptiveBoost * (1.0 - lumCurve) * 0.5);

    // Contrast (pivot around midpoint)
    color = mix(vec3(0.5), color, contrast);

    // Vibrancy (selective saturation boost scaled by existing saturation)
    float currentLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    float darkFactor = 1.0 - vibrancyDarkness * (1.0 - blurredLum);
    color = mix(vec3(currentLum), color, 1.0 + vibrancy * sat * darkFactor);

    // ========================================
    // COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // SHARP REFRACTED RIM
    // The rim band shows the *unblurred* refracted background — warping a
    // heavily blurred texture is invisible, so the visible "liquid" edge
    // comes from sharp content. Blends from frosted interior to sharp rim.
    // Applied after the frosted tint pipeline so the rim stays vivid, but
    // before fresnel/specular so edge lighting sits on top of it.
    // ========================================
    if (refractionStrength > 0.001) {
        vec3 sharpC;
        if (chromaticAberration > 0.001) {
            sharpC.r = sampleSharp(uvR).r;
            sharpC.g = sampleSharp(uvG).g;
            sharpC.b = sampleSharp(uvB).b;
        } else {
            sharpC = sampleSharp(uvG).rgb;
        }
        float rimMix = smoothstep(0.35, 0.9, edgeProximity);
        color = mix(color, sharpC, rimMix);
    }

    // ========================================
    // FRESNEL RIM GLOW (edge zone)
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = edgeProximity * edgeProximity * fresnelStrength * 0.15;
        color += vec3(1.0) * fresnel;
    }

    // ========================================
    // SPECULAR — subtle top highlight (edge zone)
    // ========================================
    if (specularStrength > 0.001) {
        float topBias = pow(max(1.0 - uv.y, 0.0), 2.0);
        float spec = topBias * edgeProximity * edgeProximity * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // INNER SHADOW (bottom rim)
    // ========================================
    {
        float bottomBias = pow(uv.y, 2.0);
        float shadow = bottomBias * edgeProximity * edgeProximity * 0.06;
        color *= 1.0 - shadow;
    }

    float glassA = glassOpacity * cornerAlpha;

    if (hasMask) {
        // Composite the rendered surface over the glass effect in a single
        // pass. surfacePixel is premultiplied alpha from Hyprland's surface
        // rendering, so we unpremultiply before the 'over' blend.
        float surfA = surfacePixel.a;

        // Windows only, focused (edgeOpacityFade folds in focus/config
        // already - a no-op everywhere else): thin the window's own opaque
        // content right at the refraction band so glass shows through more
        // strongly there, reusing edgeProximity - the same falloff that
        // already drives the refraction bezel width, so the transparent
        // band lines up with the visible refraction band exactly. Deep in
        // the interior edgeProximity -> 0, so surfA (and the content) stays
        // fully opaque - only the rim thins out.
        surfA *= 1.0 - clamp(edgeOpacityFade, 0.0, 1.0) * edgeProximity;

        vec3 surfRGB = surfA > 0.001 ? surfacePixel.rgb / surfA : vec3(0.0);

        float compA = surfA + glassA * (1.0 - surfA);
        vec3 compRGB = compA > 0.001
            ? (surfRGB * surfA + color * glassA * (1.0 - surfA)) / compA
            : vec3(0.0);

        // Hyprland's compositor expects premultiplied alpha (blend GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
        fragColor = vec4(compRGB * compA, compA);
    } else {
        // Windows: output the glass effect alone, surface is rendered separately by Hyprland.
        // Premultiplied: without this, a fading window's glass keeps full RGB contribution
        // because the GL_ONE source factor adds raw color regardless of alpha.
        fragColor = vec4(color * glassA, glassA);
    }
}
)GLSL"},

    {"gaussianblur.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 direction; // (1.0/width, 0.0) for horizontal, (0.0, 1.0/height) for vertical
uniform float blurRadius; // kernel radius in pixels

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Compute sigma from radius (covers ~3 sigma)
    float sigma = max(blurRadius / 3.0, 0.001);
    float invSigma2 = -0.5 / (sigma * sigma);

    int samples = min(int(ceil(blurRadius)), 8);

    // Center tap
    float w0 = 1.0;
    vec4 result = texture(tex, v_texcoord) * w0;
    float totalWeight = w0;

    // Linear sampling: pair adjacent taps (i, i+1) into a single bilinear fetch.
    // The interpolated offset between two texels yields their weighted average
    // in one texture() call, halving the total tap count.
    for (int i = 1; i <= samples; i += 2) {
        float x1 = float(i);
        float x2 = float(i + 1);
        float w1 = exp(x1 * x1 * invSigma2);
        float w2 = (i + 1 <= samples) ? exp(x2 * x2 * invSigma2) : 0.0;
        float wSum = w1 + w2;
        if (wSum < 0.0001) continue;

        // Offset biased toward the heavier weight
        float offset = (x1 * w1 + x2 * w2) / wSum;

        result += texture(tex, v_texcoord + direction * offset) * wSum;
        result += texture(tex, v_texcoord - direction * offset) * wSum;
        totalWeight += 2.0 * wSum;
    }

    fragColor = result / totalWeight;
}
)GLSL"},

    {"jfaseed.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Jump Flood Algorithm - seed pass.
 *
 * Marks each field texel adjacent to the layer alpha mask's boundary
 * (inside, with at least two outside 4-neighbors - see below) with its own
 * texel coordinate, encoded as (seedX, seedY, 1.0=valid) in .rgb. Every
 * other texel gets .b = 0.0 (no seed yet). The propagation passes
 * (jfastep.frag) flood these seeds outward until every texel knows its
 * nearest one.
 */

uniform sampler2D tex; // layer's rendered-surface alpha mask (temp FBO)
uniform vec2 maskUVOffset; // maps this field's [0,1] uv -> mask texture uv,
uniform vec2 maskUVScale;  // same transform liquidglass.frag's maskUV uses
uniform vec2 fieldSize;    // field buffer resolution, in texels
uniform float maskAlphaThreshold;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 myTexel = floor(v_texcoord * fieldSize);
    vec2 stepUV  = maskUVScale / fieldSize; // one field-texel step, in mask UV space
    vec2 baseUV  = v_texcoord * maskUVScale + maskUVOffset;

    float ownAlpha = texture(tex, clamp(baseUV, 0.001, 0.999)).a;
    bool  inside    = ownAlpha >= maskAlphaThreshold;

    // Require >=2 outside neighbors, not just 1: a lone outside neighbor is
    // just as likely to be single-texel noise (an anti-aliased fringe, a
    // font glyph still rasterizing, a widget mid-layout right after a
    // reload) as a real edge, and seeding on it plants a false
    // nearest-boundary seed that JFA then faithfully propagates outward -
    // visible as a small pucker of full-strength edge refraction/fresnel/
    // specular in the middle of otherwise flat glass ("bubbles"). A real
    // edge is always a multi-texel-wide run of boundary texels, so
    // requiring 2 rejects the isolated-noise case without blunting any
    // genuine edge.
    bool boundary = false;
    if (inside) {
        vec2 offs[4] = vec2[](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
        int outsideCount = 0;
        for (int i = 0; i < 4; i++) {
            vec2 nUV = clamp(baseUV + offs[i] * stepUV, 0.001, 0.999);
            if (texture(tex, nUV).a < maskAlphaThreshold) {
                outsideCount++;
                if (outsideCount >= 2) {
                    boundary = true;
                    break;
                }
            }
        }
    }

    fragColor = vec4(myTexel, boundary ? 1.0 : 0.0, 1.0);
}
)GLSL"},

    {"jfastep.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Jump Flood Algorithm - propagation pass.
 *
 * Standard 9-tap JFA step: for each field texel, look at itself plus 8
 * neighbors offset by the current step size, and adopt whichever
 * neighbor's already-known nearest-boundary seed is closest to this
 * texel's own position. Called with descending power-of-two step sizes
 * until step=1, converging to an approximate per-texel nearest-boundary
 * seed.
 */

uniform sampler2D prevBuf; // previous iteration's (seedX, seedY, validFlag)
uniform vec2 fieldSize;
uniform float stepPx;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 myTexel = floor(v_texcoord * fieldSize);

    float bestDist = 1e20;
    vec2  bestSeed = vec2(-1.0);

    vec2 offs[9] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0),
        vec2(1.0, 1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(-1.0, -1.0)
    );

    for (int i = 0; i < 9; i++) {
        vec2 sampleTexel = myTexel + offs[i] * stepPx;
        vec2 sampleUV    = (sampleTexel + 0.5) / fieldSize;
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
            continue;

        vec3 candidate = texture(prevBuf, sampleUV).rgb;
        if (candidate.b < 0.5)
            continue; // that neighbor has no seed yet

        float d = distance(myTexel, candidate.xy);
        if (d < bestDist) {
            bestDist = d;
            bestSeed = candidate.xy;
        }
    }

    fragColor = bestSeed.x >= 0.0 ? vec4(bestSeed, 1.0, 1.0) : vec4(0.0, 0.0, 0.0, 0.0);
}
)GLSL"},

    {"jfafinalize.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Jump Flood Algorithm - finalize pass.
 *
 * Converts the last propagation buffer's raw (seedX, seedY, validFlag)
 * into a *signed* per-texel distance-to-boundary value in real pixels:
 * positive inside the mask silhouette, negative outside. The sign matters
 * even though the glass shader only shades inside fragments: its gradient
 * stencil (refractionDirField) reaches several texels across the boundary,
 * and an unsigned field folds into a V at the silhouette - both sides
 * positive - so stencil arms landing outside read a *rising* distance and
 * collapse/flip the gradient. At corners both arms cross out at once,
 * asymmetrically, which showed up as diagonal seams radiating from every
 * corner. A signed field is a clean linear ramp across the boundary, so
 * the same stencil differentiates it correctly.
 *
 * Written to its own texture left at the default GL_LINEAR filtering
 * (unlike the seed/step ping-pong buffers, which must stay GL_NEAREST) -
 * bilinearly interpolating a real distance value between texels is
 * meaningful (and across the signed zero crossing, exactly right) and
 * smooths curved silhouettes; interpolating raw seed *positions* is not
 * and would corrupt them.
 */

uniform sampler2D prevBuf; // final (seedX, seedY, validFlag) buffer
uniform sampler2D maskTex; // layer's alpha mask - decides the sign
uniform vec2 maskUVOffset; // same field-uv -> mask-uv transform as jfaseed
uniform vec2 maskUVScale;
uniform float maskAlphaThreshold;
uniform vec2 fieldSize;
uniform float pixelsPerTexel;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 myTexel = floor(v_texcoord * fieldSize);
    vec3 seed = texture(prevBuf, v_texcoord).rgb;

    float dist = seed.b < 0.5
        ? 9999.0 // no boundary seed reached this texel (pathological
                  // sliver-thin content) - treat as deep interior/exterior,
                  // flat glass either way
        : length(myTexel - seed.xy) * pixelsPerTexel;

    vec2 maskUV = v_texcoord * maskUVScale + maskUVOffset;
    bool inside = texture(maskTex, clamp(maskUV, 0.001, 0.999)).a >= maskAlphaThreshold;

    float signedDist = inside ? dist : -dist;
    fragColor = vec4(signedDist, signedDist, signedDist, 1.0);
}
)GLSL"},
};
