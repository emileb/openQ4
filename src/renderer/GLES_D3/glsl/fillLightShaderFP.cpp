// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesFillLightShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 fill light -- fragment stage, diii4a's _BFG globalIllumination:
// squared half-Lambert from the fixed direction, a Blinn highlight at
// exponent 10, both scaled by the fill brightness (uShadingParams.x, 0.3 in
// diii4a). Added onto the black the depth prepass wrote, before the real
// lights; no branches.
//
// Units 1, 4, 5 match the interaction program's bump, diffuse and specular
// samplers, so the same image binding code serves both.

uniform sampler2D uBumpMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform vec4 uShadingParams;

in vec2 vBumpTexCoord;
in vec2 vDiffuseTexCoord;
in vec2 vSpecularTexCoord;
in vec3 vLightVector;
in vec3 vHalfAngleVector;
in vec3 vVertexColor;

out vec4 outColor;

void main() {
    vec4 bumpSample = texture(uBumpMap, vBumpTexCoord);
    // the same X-from-alpha, Y-from-green decode as the interaction program,
    // with Z rebuilt so a two-channel bump format behaves too
    vec2 xy = vec2(bumpSample.a, bumpSample.g) * 2.0 - 1.0;
    vec3 normal = normalize(vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0))));

    vec3 lightDir = normalize(vLightVector);
    float halfLambert = dot(normal, lightDir) * 0.5 + 0.5;
    halfLambert *= halfLambert;

    float specularDot = abs(dot(normalize(vHalfAngleVector), normal));
    vec3 specular = texture(uSpecularMap, vSpecularTexCoord).rgb * uSpecularColor.rgb * pow(specularDot, 10.0);
    vec3 diffuse = texture(uDiffuseMap, vDiffuseTexCoord).rgb * uDiffuseColor.rgb;

    outColor = vec4((diffuse + specular) * halfLambert * uShadingParams.x * vVertexColor, 0.0);
}
)";
