// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesShadowCasterShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 shadow-map caster -- vertex stage. Ported from
// Vulkan/shaders/shadow_caster.vert (projected) and shadow_point_caster.vert
// (GLESD3_SHADOW_POINT).
//
// Projected: uMVP is model -> light clip (R_ShadowMapClipPlanesToGLMatrix,
// whose z row is zero), and the depth the map stores is not clip z but the
// linear falloff-plane depth dot(position, uShadowDepthRow), written by the
// fragment stage -- the same value the receiver compares against.
//
// Point: uMVP is model -> cube-face view space, and the clip position is built
// here from the GL perspective terms in uShadowDepthRow.xy. Unlike the Vulkan
// port these are the plain GL A/B terms; there is no clip-z fixup to fold in.
// The stored depth is radial distance over the far envelope.

layout(location = 0) in vec3 inPosition;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;
uniform vec4 uShadowDepthRow;

#ifdef GLESD3_ALPHATEST
uniform vec4 uTexMatrixS;
uniform vec4 uTexMatrixT;
out vec2 vAlphaTexCoord;
#endif

#ifdef GLESD3_SHADOW_POINT
out vec3 vPointShadowVector;
#else
out float vShadowDepth;
#endif

void main() {
    vec4 position = vec4(inPosition, 1.0);
#ifdef GLESD3_ALPHATEST
    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);
    vAlphaTexCoord = vec2(dot(texCoord, uTexMatrixS), dot(texCoord, uTexMatrixT));
#endif
#ifdef GLESD3_SHADOW_POINT
    vec4 viewPos = uMVP * position;
    vPointShadowVector = viewPos.xyz;
    gl_Position = vec4(viewPos.xy, uShadowDepthRow.x * viewPos.z + uShadowDepthRow.y * viewPos.w, -viewPos.z);
#else
    vShadowDepth = dot(position, uShadowDepthRow);
    gl_Position = uMVP * position;
#endif
}
)";
