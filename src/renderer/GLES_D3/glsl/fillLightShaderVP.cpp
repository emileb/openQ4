// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesFillLightShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- this pass
// draws at depth EQUAL against the prepass.
invariant gl_Position;

// gles_d3 fill light (r_glesD3FillLight) -- vertex stage. A port of diii4a's
// harm_r_globalIllumination (renderer/glsl/shader300/doom3/globalIllumination.*,
// its _BFG variant): not global illumination at all, but a light from a
// fixed MODEL-SPACE direction, normalize(0, 0.5, 1), applied to every opaque
// surface so that areas Quake 4 leaves unlit are never pure black.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;
uniform vec4 uLocalViewOrigin;
uniform vec4 uBumpMatrixS;
uniform vec4 uBumpMatrixT;
uniform vec4 uDiffuseMatrixS;
uniform vec4 uDiffuseMatrixT;
uniform vec4 uSpecularMatrixS;
uniform vec4 uSpecularMatrixT;
// the SVC packing, as in the interaction program
uniform vec4 uVertexColor;

out vec2 vBumpTexCoord;
out vec2 vDiffuseTexCoord;
out vec2 vSpecularTexCoord;
out vec3 vLightVector;
out vec3 vHalfAngleVector;
out vec3 vVertexColor;

vec3 TangentSpaceVector(vec3 objectVector) {
    return vec3(
        dot(inTangent, objectVector),
        dot(inBitangent, objectVector),
        dot(inNormal, objectVector));
}

void main() {
    vec4 position = vec4(inPosition, 1.0);
    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);

    vec3 toLight = normalize(vec3(0.0, 0.5, 1.0));
    vec3 toView = uLocalViewOrigin.xyz - position.xyz;
    vLightVector = TangentSpaceVector(toLight);
    vHalfAngleVector = TangentSpaceVector(toLight + normalize(toView));

    vBumpTexCoord = vec2(dot(texCoord, uBumpMatrixS), dot(texCoord, uBumpMatrixT));
    vDiffuseTexCoord = vec2(dot(texCoord, uDiffuseMatrixS), dot(texCoord, uDiffuseMatrixT));
    vSpecularTexCoord = vec2(dot(texCoord, uSpecularMatrixS), dot(texCoord, uSpecularMatrixT));

    vVertexColor = inColor.rgb * uVertexColor.x + vec3(uVertexColor.y);

    gl_Position = uMVP * position;
}
)";
