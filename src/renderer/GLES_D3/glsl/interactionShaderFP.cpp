// Copyright (C) 2026 DarkMatter Productions
//
// Per-light bump/diffuse/specular -- fragment stage.
//
// A direct port of src/renderer/Vulkan/shaders/interaction.frag. Everything
// Quake 4 specific comes across unchanged; listed here so a later divergence
// shows up as a diff rather than as a defect:
//
//   - DXT5/RXGB bump decode: alpha=x, green=y, blue=z, no renormalization
//   - specular through the REAL _specularTable ramp, x2 because the CPU-side
//     ARB2 path doubles the specular env constant
//   - projected light falloff and light projection sampled with textureProj
//   - direction vectors normalized in-shader; no normalization cube map
//   - additive ONE:ONE, alpha written as 0 like the GL reference
//
// Ambient lights are why nothing in Quake 4 is ever fully black. They are
// ordinary lights flagged ambientLight, and every path substitutes a constant
// tangent-space direction for the per-pixel light vector: ARB2 by binding
// ambientNormalMap instead of the normalization cube map
// (draw_arb2.cpp:11460), this shader by uAmbientLight / uAmbientDir. The
// cube's 8-bit quantization is applied CPU-side so the two agree exactly.
//
// Sampler units are 1:1 with the Vulkan descriptor sets:
//   0 specularTable   1 bump      2 lightFalloff
//   3 lightProjection 4 diffuse   5 specular

#include "glsl_shaders.h"

const char * const glesInteractionShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

uniform sampler2D uSpecularTableMap;
uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;

uniform float uAmbientLight;
uniform vec3 uAmbientDir;

in vec2 vBumpTexCoord;
in vec2 vDiffuseTexCoord;
in vec2 vSpecularTexCoord;
in vec4 vLightFalloffTexCoord;
in vec4 vLightProjectionTexCoord;
in vec3 vLightVector;
in vec3 vHalfAngleVector;
in vec3 vVertexColor;
in vec3 vViewVector;

out vec4 outColor;

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

void main() {
    vec4 bumpSample = texture(uBumpMap, vBumpTexCoord);

    // RXGB / DXT5nm: x in alpha, y in green, z in blue, no renormalization
    vec3 localNormal = vec3(bumpSample.a, bumpSample.g, bumpSample.b) * 2.0 - 1.0;

    // an ambient light lights from a constant direction instead of from the
    // light origin, which is what keeps unlit corners off pure black
    vec3 lightDir = (uAmbientLight > 0.5) ? uAmbientDir : SafeNormalize(vLightVector);
    float ndotl = max(dot(lightDir, localNormal), 0.0);

    vec3 light = vec3(ndotl);
    light *= textureProj(uLightFalloffMap, vLightFalloffTexCoord).rgb;
    light *= textureProj(uLightProjectionMap, vLightProjectionTexCoord).rgb;

    vec3 diffuse = texture(uDiffuseMap, vDiffuseTexCoord).rgb * uDiffuseColor.rgb;

    vec3 halfAngle = SafeNormalize(vHalfAngleVector);
    float specularDot = clamp(dot(halfAngle, localNormal), 0.0, 1.0);
    float specularTerm = texture(uSpecularTableMap, vec2(specularDot, 0.5)).r * 2.0;
    vec3 specular = texture(uSpecularMap, vSpecularTexCoord).rgb * uSpecularColor.rgb * specularTerm;

    outColor = vec4((diffuse + specular) * light * vVertexColor, 0.0);
}
)";
