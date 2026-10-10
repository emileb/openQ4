// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesDisplacementCubeShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 glsl/DisplacementCube.glsl -- vertex stage. Ported from
// Vulkan/shaders/material_displacement_cube.vert (retail
// displacementcube.glslvp), which moved the displacement coordinate
// transform per-vertex.
//
// uParms slots: 0 scrollX, 1 scrollY, 2 sizeX, 3 sizeY, 4 texCoordSize,
// 5 EyeVector. EyeVector is the GLOBAL view origin while inPosition is
// model-local, exactly as retail computes it; for world geometry the two
// spaces coincide.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;
uniform vec4 uColor;
uniform vec4 uVertexColor;
uniform vec4 uParms[16];

out vec2 vTexCoord;
out vec2 vDisplacementTexCoord;
out vec4 vColor;
out vec3 vNormal;
out vec3 vViewVector;

void main() {
    vTexCoord = inTexCoord;

    vec2 newSize = vec2(uParms[2].x, uParms[3].x) * 0.5 + vec2(1.5);
    vec2 newScroll = vec2(uParms[0].x, uParms[1].x) * 0.2;
    vDisplacementTexCoord = (inTexCoord - vec2(0.5)) * newSize + newScroll + vec2(0.5);

    vec4 vertexTerm = vec4(inColor.rgb * uVertexColor.x + uVertexColor.y,
                           inColor.a   * uVertexColor.z + uVertexColor.w);
    vColor = vertexTerm * uColor;
    vNormal = inNormal;
    vViewVector = uParms[5].xyz - inPosition;

    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
