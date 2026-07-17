#include "GlassRenderer.hpp"
#include "BuiltInPresets.hpp"
#include "Globals.hpp"

#include <array>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace GlassRenderer {

static GLuint fbId(const SP<Render::IFramebuffer>& framebuffer) {
    return dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get())->getFBID();
}

static void uploadThemeUniforms(const SResolveContext& ctx) {
    const auto& uniforms = g_pGlobalState->shaderManager.glassUniforms;
    const auto& glassShader = g_pGlobalState->shaderManager.glassShader;
    const auto& defaults = ctx.isDark ? DARK_THEME_DEFAULTS : LIGHT_THEME_DEFAULTS;

    glassShader->setUniformFloat(SHADER_BRIGHTNESS, resolvePresetFloat(ctx, &SPresetValues::brightness, &SOverridableConfig::brightness, defaults.brightness));
    glassShader->setUniformFloat(SHADER_CONTRAST,   resolvePresetFloat(ctx, &SPresetValues::contrast, &SOverridableConfig::contrast, defaults.contrast));
    glUniform1f(uniforms.saturation,                 resolvePresetFloat(ctx, &SPresetValues::saturation, &SOverridableConfig::saturation, defaults.saturation));
    glassShader->setUniformFloat(SHADER_VIBRANCY,   resolvePresetFloat(ctx, &SPresetValues::vibrancy, &SOverridableConfig::vibrancy, defaults.vibrancy));
    glUniform1f(uniforms.vibrancyDarkness,           resolvePresetFloat(ctx, &SPresetValues::vibrancyDarkness, &SOverridableConfig::vibrancyDarkness, defaults.vibrancyDarkness));

    glUniform1f(uniforms.adaptiveDim,   resolvePresetFloat(ctx, &SPresetValues::adaptiveDim, &SOverridableConfig::adaptiveDim, defaults.adaptiveDim));
    glUniform1f(uniforms.adaptiveBoost, resolvePresetFloat(ctx, &SPresetValues::adaptiveBoost, &SOverridableConfig::adaptiveBoost, defaults.adaptiveBoost));
}

void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, SSampleLayout& outLayout, int downscale,
                       SP<Render::IFramebuffer>* sharpFramebuffer, const CRegion* partialDamage) {
    if (!sourceFramebuffer)
        return;
    const int pad = SAMPLE_PADDING_PX;
    int fullWidth  = static_cast<int>(box.width) + 2 * pad;
    int fullHeight = static_cast<int>(box.height) + 2 * pad;

    // Allocate sample FBO at reduced resolution when blur is strong enough
    // to hide the lower resolution. Weak blur at half-res shows pixelation.
    int sampleWidth  = std::max(1, fullWidth / downscale);
    int sampleHeight = std::max(1, fullHeight / downscale);

    if (!sampleFramebuffer)
        sampleFramebuffer = g_pHyprRenderer->createFB("hyprglass-sample");

    if (sampleFramebuffer->m_size.x != sampleWidth || sampleFramebuffer->m_size.y != sampleHeight)
        sampleFramebuffer->alloc(sampleWidth, sampleHeight, sourceFramebuffer->m_drmFormat);

    int srcX0 = static_cast<int>(box.x) - pad;
    int srcX1 = static_cast<int>(box.x + box.width) + pad;
    int srcY0 = static_cast<int>(box.y) - pad;
    int srcY1 = static_cast<int>(box.y + box.height) + pad;

    // Clamp source coordinates to framebuffer bounds to avoid reading black/undefined pixels
    int framebufferWidth  = static_cast<int>(sourceFramebuffer->m_size.x);
    int framebufferHeight = static_cast<int>(sourceFramebuffer->m_size.y);

    // Destination coords in downscaled FBO space
    int dstX0 = 0, dstY0 = 0, dstX1 = sampleWidth, dstY1 = sampleHeight;

    // Scale destination adjustments proportionally for the downscaled FBO
    const float xScale = static_cast<float>(sampleWidth) / fullWidth;
    const float yScale = static_cast<float>(sampleHeight) / fullHeight;

    if (srcX0 < 0) { dstX0 += static_cast<int>(-srcX0 * xScale); srcX0 = 0; }
    if (srcY0 < 0) { dstY0 += static_cast<int>(-srcY0 * yScale); srcY0 = 0; }
    if (srcX1 > framebufferWidth)  { dstX1 -= static_cast<int>((srcX1 - framebufferWidth) * xScale);  srcX1 = framebufferWidth; }
    if (srcY1 > framebufferHeight) { dstY1 -= static_cast<int>((srcY1 - framebufferHeight) * yScale); srcY1 = framebufferHeight; }

    // Padding ratio is relative to the logical content area (resolution-independent)
    outLayout.paddingRatio = Vector2D(
        static_cast<double>(pad) / fullWidth,
        static_cast<double>(pad) / fullHeight
    );

    // Valid-content bounds in [0,1] UV of the padded region: where the source
    // clamping above cut the blit short (window near a screen edge), the rest
    // of the FBO is cleared black. Resolution-independent, so the same bounds
    // apply to the (possibly downscaled) blur sample and the full-res sharp
    // copy. The shader clamps its sampling here so refraction stretches edge
    // content instead of pulling in black.
    const int fullDstX0 = srcX0 - (static_cast<int>(box.x) - pad);
    const int fullDstY0 = srcY0 - (static_cast<int>(box.y) - pad);
    outLayout.validMin = Vector2D(
        static_cast<double>(fullDstX0) / fullWidth,
        static_cast<double>(fullDstY0) / fullHeight);
    outLayout.validMax = Vector2D(
        static_cast<double>(fullDstX0 + (srcX1 - srcX0)) / fullWidth,
        static_cast<double>(fullDstY0 + (srcY1 - srcY0)) / fullHeight);

    // The render pass scissors each element to its damage region.
    // That scissor state leaks here and clips glBlitFramebuffer on the
    // DRAW framebuffer, causing partial writes and stale noise artifacts.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);

    if (sharpFramebuffer) {
        // The sharp FBO is the *authoritative unblurred cache* of the
        // background: a full capture when the sampled geometry changes, and
        // per-damage-rect updates otherwise (partialDamage, buffer coords).
        // Only freshly-rendered rects are ever read from the source FB -
        // outside the frame's damage the FB holds last frame's finished
        // image including the caller's own window, which must never leak
        // into the sample. The (possibly downscaled) blur input is then
        // re-derived from the sharp cache, so content changes beneath the
        // glass show through immediately.
        if (!*sharpFramebuffer)
            *sharpFramebuffer = g_pHyprRenderer->createFB("hyprglass-sharp");

        bool fullCapture = !partialDamage;
        if ((*sharpFramebuffer)->m_size.x != fullWidth || (*sharpFramebuffer)->m_size.y != fullHeight) {
            (*sharpFramebuffer)->alloc(fullWidth, fullHeight, sourceFramebuffer->m_drmFormat);
            fullCapture = true; // freshly allocated: previous content is gone
        }

        const int offX = static_cast<int>(box.x) - pad;
        const int offY = static_cast<int>(box.y) - pad;

        glBindFramebuffer(GL_FRAMEBUFFER, fbId(*sharpFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbId(sourceFramebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbId(*sharpFramebuffer));

        if (fullCapture) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                              fullDstX0, fullDstY0,
                              fullDstX0 + (srcX1 - srcX0), fullDstY0 + (srcY1 - srcY0),
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        } else {
            for (const auto& r : partialDamage->getRects()) {
                const int rx0 = std::max(static_cast<int>(r.x1), srcX0);
                const int ry0 = std::max(static_cast<int>(r.y1), srcY0);
                const int rx1 = std::min(static_cast<int>(r.x2), srcX1);
                const int ry1 = std::min(static_cast<int>(r.y2), srcY1);
                if (rx0 >= rx1 || ry0 >= ry1)
                    continue;
                glBlitFramebuffer(rx0, ry0, rx1, ry1,
                                  rx0 - offX, ry0 - offY, rx1 - offX, ry1 - offY,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
        }

        // Re-derive the blur input from the sharp cache (downscales when the
        // blur is strong enough to hide it).
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(sampleFramebuffer));
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbId(*sharpFramebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbId(sampleFramebuffer));
        glBlitFramebuffer(0, 0, fullWidth, fullHeight,
                          0, 0, sampleWidth, sampleHeight,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        return;
    }

    // No sharp cache requested: legacy direct full capture.
    glBindFramebuffer(GL_FRAMEBUFFER, fbId(sampleFramebuffer));
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbId(sourceFramebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbId(sampleFramebuffer));
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations,
                    GLuint callerFramebufferID, int viewportWidth, int viewportHeight) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!sampleFramebuffer || radius <= 0.0f || iterations <= 0 || !shaderManager.isInitialized())
        return;

    int width  = static_cast<int>(sampleFramebuffer->m_size.x);
    int height = static_cast<int>(sampleFramebuffer->m_size.y);

    auto& blurTempFramebuffer = g_pGlobalState->blurTempFramebuffer;
    if (!blurTempFramebuffer)
        blurTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-blur-temp");

    if (blurTempFramebuffer->m_size.x != width || blurTempFramebuffer->m_size.y != height)
        blurTempFramebuffer->alloc(width, height, sampleFramebuffer->m_drmFormat);

    // Fullscreen quad projection: maps VAO positions [0,1] to clip space [-1,1]
    static constexpr std::array<float, 9> FULLSCREEN_PROJECTION = {
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f,
       -1.0f,-1.0f, 1.0f,
    };

    const auto& blurUniforms = shaderManager.blurUniforms;

    auto shader = g_pHyprOpenGL->useShader(shaderManager.blurShader);
    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
    shader->setUniformInt(SHADER_TEX, 0);
    glUniform1f(blurUniforms.radius, radius);
    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    g_pHyprOpenGL->setViewport(0, 0, width, height);
    glActiveTexture(GL_TEXTURE0);

    // Ping-pong at full resolution: sampleFramebuffer ↔ blurTempFramebuffer
    for (int iteration = 0; iteration < iterations; iteration++) {
        // Horizontal pass: sampleFramebuffer → blurTempFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(blurTempFramebuffer));
        sampleFramebuffer->getTexture()->bind();
        glUniform2f(blurUniforms.direction, 1.0f / width, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Vertical pass: blurTempFramebuffer → sampleFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(sampleFramebuffer));
        blurTempFramebuffer->getTexture()->bind();
        glUniform2f(blurUniforms.direction, 0.0f, 1.0f / height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Restore caller's GL state without querying (avoids pipeline stalls)
    glBindFramebuffer(GL_FRAMEBUFFER, callerFramebufferID);
    glBindVertexArray(0);
    g_pHyprOpenGL->setViewport(0, 0, viewportWidth, viewportHeight);
}

void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const SSampleLayout& sampleLayout, const SResolveContext& resolveContext,
                       const SMaskInfo* mask, SP<Render::IFramebuffer> sharpFramebuffer,
                       bool refractOutward) {
    if (!sampleFramebuffer || !targetFramebuffer)
        return;

    auto& shaderManager  = g_pGlobalState->shaderManager;
    const auto& uniforms = shaderManager.glassUniforms;

    // Pass the inverted monitor transform explicitly: both textures we sample
    // (the blitted blur sample and the temp-FBO mask) hold content in *buffer*
    // orientation (blitted/rendered straight from/into buffer space), unlike
    // surface textures which are stored logically. The transform argument
    // rotates the quad's texcoord<->position correspondence (projectBox rotates
    // about the unit-square center - vertex positions still cover the same
    // box) so UVs traverse the content in buffer orientation. Same pattern as
    // core's renderTextureInternal passing invertTransform(texture->m_transform)
    // for pre-transformed buffer content. Without this, content composites
    // transposed/mirrored on rotated monitors. (An earlier session removed
    // this arg blaming it for "zoomed/clipped" rotation corruption - that was
    // actually caused by m_transformedSize/m_pixelSize mismatches in the temp
    // FBO, viewport restore, and mask UV normalization, since fixed.)
    const auto contentTransform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprRenderer->m_renderData.pMonitor->m_transform));
    Mat3x3 glMatrix = g_pHyprRenderer->projectBoxToTarget(rawBox, contentTransform);
    auto texture    = sampleFramebuffer->getTexture();

    glMatrix.transpose();

    glBindFramebuffer(GL_FRAMEBUFFER, fbId(targetFramebuffer));
    glActiveTexture(GL_TEXTURE0);
    texture->bind();

    // Layers only: bind the temp FBO texture (rendered surface) on texture unit 1.
    // The shader samples it to mask glass to visible content and composite surface on top.
    // Windows pass mask=nullptr so this block is skipped.
    if (mask && mask->textureId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(mask->target, mask->textureId);
        glActiveTexture(GL_TEXTURE0);
    }

    // Unblurred background copy for the refracted rim on texture unit 2.
    // Fall back to the blurred sample if the caller has no sharp copy (the
    // rim then just warps blur, the pre-sharp-rim behavior).
    {
        auto sharpTexture = sharpFramebuffer ? sharpFramebuffer->getTexture() : texture;
        glActiveTexture(GL_TEXTURE2);
        sharpTexture->bind();
        glActiveTexture(GL_TEXTURE0);
    }

    auto shader = g_pHyprOpenGL->useShader(shaderManager.glassShader);

    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, glMatrix.getMatrix());
    shader->setUniformInt(SHADER_TEX, 0);
    glUniform1i(uniforms.sharpTex, 2);

    const auto fullSize = Vector2D(transformedBox.width, transformedBox.height);
    shader->setUniformFloat2(SHADER_FULL_SIZE,
        static_cast<float>(fullSize.x), static_cast<float>(fullSize.y));

    glUniform1f(uniforms.refractionStrength,  resolvePresetFloat(resolveContext, &SPresetValues::refractionStrength, &SOverridableConfig::refractionStrength));
    glUniform1f(uniforms.chromaticAberration, resolvePresetFloat(resolveContext, &SPresetValues::chromaticAberration, &SOverridableConfig::chromaticAberration));
    glUniform1f(uniforms.fresnelStrength,     resolvePresetFloat(resolveContext, &SPresetValues::fresnelStrength, &SOverridableConfig::fresnelStrength));
    glUniform1f(uniforms.specularStrength,    resolvePresetFloat(resolveContext, &SPresetValues::specularStrength, &SOverridableConfig::specularStrength));
    glUniform1f(uniforms.glassOpacity,        resolvePresetFloat(resolveContext, &SPresetValues::glassOpacity, &SOverridableConfig::glassOpacity) * alpha);
    glUniform1f(uniforms.edgeThickness,       resolvePresetFloat(resolveContext, &SPresetValues::edgeThickness, &SOverridableConfig::edgeThickness));
    glUniform1f(uniforms.lensDistortion,      resolvePresetFloat(resolveContext, &SPresetValues::lensDistortion, &SOverridableConfig::lensDistortion));

    uploadThemeUniforms(resolveContext);

    const int64_t tintColorValue = resolvePresetInt(resolveContext, &SPresetValues::tintColor, &SOverridableConfig::tintColor);
    glUniform3f(uniforms.tintColor,
        static_cast<float>((tintColorValue >> 24) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 16) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 8) & 0xFF) / 255.0f);
    glUniform1f(uniforms.tintAlpha,
        static_cast<float>(tintColorValue & 0xFF) / 255.0f);

    glUniform2f(uniforms.uvPadding,
        static_cast<float>(sampleLayout.paddingRatio.x),
        static_cast<float>(sampleLayout.paddingRatio.y));
    glUniform4f(uniforms.validBounds,
        static_cast<float>(sampleLayout.validMin.x),
        static_cast<float>(sampleLayout.validMin.y),
        static_cast<float>(sampleLayout.validMax.x),
        static_cast<float>(sampleLayout.validMax.y));
    glUniform1f(uniforms.refractionDirSign, refractOutward ? 1.0f : -1.0f);

    // Layers only: enable mask and provide UV mapping from the glass quad into
    // the monitor-sized temp FBO. Windows use useMask=0 (no masking).
    if (mask && mask->textureId != 0) {
        glUniform1i(uniforms.useMask, 1);
        glUniform1i(uniforms.maskTex, 1);
        glUniform2f(uniforms.maskUVOffset,
            static_cast<float>(mask->uvOffset.x),
            static_cast<float>(mask->uvOffset.y));
        glUniform2f(uniforms.maskUVScale,
            static_cast<float>(mask->uvScale.x),
            static_cast<float>(mask->uvScale.y));
        glUniform1f(uniforms.maskAlphaThreshold, mask->alphaThreshold);
    } else {
        glUniform1i(uniforms.useMask, 0);
        glUniform1f(uniforms.maskAlphaThreshold, 0.001f);
    }

    shader->setUniformFloat(SHADER_RADIUS, cornerRadius);
    shader->setUniformFloat(SHADER_ROUNDING_POWER, roundingPower);

    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    g_pHyprOpenGL->scissor(rawBox);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_pHyprOpenGL->scissor(nullptr);
}

} // namespace GlassRenderer
