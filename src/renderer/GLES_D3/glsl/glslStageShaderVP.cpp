// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesGlslStageShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 Raven GLSL material stages -- the vertex stage shared by
// MultiplyBlend and Displacement. Ported from Vulkan/shaders/
// material_multiply_blend.vert and material_displacement.vert, which are
// identical; both port the retail glprogs/glsl/*.glslvp, which read only
// ftransform(), gl_MultiTexCoord0 and gl_Color.
//
// gl_Color is the stage colour times the resolved vertex colour, using the
// same (rgbMul, rgbAdd, alphaMul, alphaAdd) packing as the material program.
// A decal stage bakes its stage colour into its own colour stream, so the
// caller passes white for uColor there.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;
uniform vec4 uColor;
uniform vec4 uVertexColor;

out vec2 vTexCoord;
out vec4 vColor;

void main() {
    vec4 vertexTerm = vec4(inColor.rgb * uVertexColor.x + uVertexColor.y,
                           inColor.a   * uVertexColor.z + uVertexColor.w);
    vColor = vertexTerm * uColor;
    vTexCoord = inTexCoord;
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
