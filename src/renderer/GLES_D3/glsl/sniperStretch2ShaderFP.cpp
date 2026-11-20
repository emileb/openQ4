// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesSniperStretch2ShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 glsl/SniperStretch2.glsl -- fragment stage. Ported from
// Vulkan/shaders/material_sniper_stretch2.frag (retail sniperstretch2.glslfp):
// the radial blur at the edge of the zoomed machine gun and sniper scopes.
//
// uParms slots:
//   0 textureScale, 1 textureHalfScale, 2 backgroundColor -- the retail contract
//   13 viewport origin, 14 viewport size, 15 _currentRender texture scale --
//      filled by the renderer for openQ4's hs/SniperBlur* materials, which
//      locate the screen copy from the fragment position instead
//
// GL window coordinates are already bottom-left, so unlike the Vulkan port
// there is no framebuffer-height flip; slot 12 is unused here.

uniform sampler2D uTexture0;	// BackgroundImage (_currentRender)
uniform sampler2D uTexture1;	// Scope
uniform vec4 uParms[16];

in vec2 vTexCoord;
in vec2 vScopeTexCoord;

out vec4 fragColor;

bool HasViewportBuiltins() {
    return all(greaterThan(uParms[14].xy, vec2(0.0)))
        && all(greaterThan(uParms[15].xy, vec2(0.0)));
}

vec2 CurrentRenderCoord(out vec2 halfTextureScale) {
    if (!HasViewportBuiltins()) {
        halfTextureScale = uParms[1].xy;
        return vTexCoord;
    }
    vec2 viewportOrigin = uParms[13].xy;
    vec2 viewportSize = max(uParms[14].xy, vec2(1.0));
    vec2 currentRenderScale = uParms[15].xy;

    vec2 viewportCoord = (gl_FragCoord.xy - viewportOrigin) / viewportSize;
    halfTextureScale = currentRenderScale * 0.5;
    return clamp(viewportCoord * currentRenderScale, vec2(0.0), currentRenderScale);
}

void main() {
    vec4 scopeArea = texture(uTexture1, vScopeTexCoord);

    vec2 halfTextureScale;
    vec2 start = CurrentRenderCoord(halfTextureScale);
    vec2 blurDir = start - halfTextureScale;
    float blurDirLength = length(blurDir);
    blurDir = blurDirLength > 0.0 ? blurDir / blurDirLength : vec2(0.0);

    if (scopeArea.r == 0.0) {
        fragColor = texture(uTexture0, start);
        return;
    }

    // the retail six-tap radial offsets and weighting, unchanged
    vec2 amount = blurDir * scopeArea.r * 0.25;
    vec4 blurColor =
        texture(uTexture0, start - amount * 1.5)
        + texture(uTexture0, start - amount * 1.4)
        + texture(uTexture0, start - amount * 1.3)
        + texture(uTexture0, start - amount * 1.2)
        + texture(uTexture0, start - amount * 1.1)
        + texture(uTexture0, start - amount * 1.0);
    blurColor /= 6.0;
    fragColor = (blurColor + uParms[2]) * 0.5;
}
)";
