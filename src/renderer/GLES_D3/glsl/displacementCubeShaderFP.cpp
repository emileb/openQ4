// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesDisplacementCubeShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 glsl/DisplacementCube.glsl -- fragment stage. Ported from
// Vulkan/shaders/material_displacement_cube.frag (retail
// displacementcube.glslfp): a displaced cube reflection added to the
// displaced surface image. The fuzz shields in waste and the blood pools.
//
// The cube map is on unit 2, sampled as samplerCube; no other sampler in this
// program uses that unit, which ES requires.

uniform sampler2D uTexture0;	// Image
uniform sampler2D uTexture1;	// DisplacementMap
uniform samplerCube uTexture2;	// CubeImage
uniform vec4 uParms[16];

in vec2 vTexCoord;
in vec2 vDisplacementTexCoord;
in vec4 vColor;
in vec3 vNormal;
in vec3 vViewVector;

out vec4 fragColor;

void main() {
    vec2 offset = texture(uTexture1, vDisplacementTexCoord).xy * uParms[4].x;

    // Raven's object-space reflection with its deliberately unnormalised view
    // vector; the cube lookup normalises the final direction itself.
    vec3 normal = normalize(vNormal);
    vec3 reflectionVector = reflect(-vViewVector, normal);
    vec3 cubeOffset = vec3(offset.x + offset.y, offset.x, offset.y) * 128.0;
    vec4 reflected = texture(uTexture2, reflectionVector + cubeOffset);
    vec4 straight = texture(uTexture0, vTexCoord + offset);

    fragColor = vec4(reflected.rgb + straight.rgb, 1.0) * vColor;
}
)";
