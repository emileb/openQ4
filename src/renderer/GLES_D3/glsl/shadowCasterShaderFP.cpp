// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesShadowCasterShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 shadow-map caster -- fragment stage. Ported from
// Vulkan/shaders/shadow_caster.frag and shadow_point_caster.frag.
//
// Depth is written from here, so glPolygonOffset is inert; the classic
// slope-scale caster offset is applied with derivatives instead, exactly as
// the GL and Vulkan casters do:
//   uShadowCasterOffset.x  r_shadowMapPolygonFactor
//   uShadowCasterOffset.y  r_shadowMapPolygonOffset in 2^-24 depth steps
//   uShadowCasterOffset.z  point far envelope (point casters only)
//   uShadowCasterOffset.w  alpha mode: -1 LESS, 1 GREATER, 2 EQUAL
//
// The ALPHATEST variant cuts perforated casters with the material's own alpha
// map; uColor.a is the stage's alpha scale, uAlphaTest its reference. Writing
// gl_FragDepth already disables early-Z here, so the base variant omitting the
// discard matters less than elsewhere -- it is still split for symmetry.

uniform vec4 uShadowCasterOffset;

#ifdef GLESD3_ALPHATEST
uniform sampler2D uTexture0;
uniform vec4 uColor;
uniform float uAlphaTest;
in vec2 vAlphaTexCoord;
#endif

#ifdef GLESD3_SHADOW_POINT
in vec3 vPointShadowVector;
#else
in float vShadowDepth;
#endif

void main() {
#ifdef GLESD3_SHADOW_POINT
    float depth = length(vPointShadowVector) / uShadowCasterOffset.z;
#else
    float depth = vShadowDepth;
#endif
    // derivatives need the whole quad, so before any discard
    float depthSlope = max(abs(dFdx(depth)), abs(dFdy(depth)));

#ifdef GLESD3_ALPHATEST
    float alpha = texture(uTexture0, vAlphaTexCoord).a * uColor.a;
    bool passed;
    if (uShadowCasterOffset.w < -0.5) {
        passed = alpha < uAlphaTest;
    } else if (uShadowCasterOffset.w > 1.5) {
        passed = abs(alpha - uAlphaTest) <= (0.5 / 255.0);
    } else {
        passed = alpha > uAlphaTest;
    }
    if (!passed) {
        discard;
    }
#endif

#ifndef GLESD3_SHADOW_POINT
    // apex side of a projector: cannot occlude anything inside the light
    if (depth <= 0.0) {
        discard;
    }
#endif

    gl_FragDepth = clamp(depth + uShadowCasterOffset.x * depthSlope + uShadowCasterOffset.y, 0.0, 1.0);
}
)";
