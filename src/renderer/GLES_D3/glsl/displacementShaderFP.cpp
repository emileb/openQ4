// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesDisplacementShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 glsl/Displacement.glsl -- fragment stage. Ported from
// Vulkan/shaders/material_displacement.frag (retail displacement.glslfp).
//
// The scrolling refraction on fluid tanks, blood pools and energy glass: a
// displacement map, scaled and scrolled by the material's shaderParms,
// offsets the lookup into the surface's own Image.
//
// uParms slots (the C side packs the authored names into these):
//   0 scrollX, 1 scrollY, 2 sizeX, 3 sizeY, 4 texCoordSize

uniform sampler2D uTexture0;	// Image
uniform sampler2D uTexture1;	// DisplacementMap
uniform vec4 uParms[16];

in vec2 vTexCoord;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec2 newSize = vec2(uParms[2].x, uParms[3].x) * 0.5 + vec2(1.5);
    vec2 newScroll = vec2(uParms[0].x, uParms[1].x) * 0.2;

    vec2 displacementCoord = (vTexCoord - vec2(0.5)) * newSize + newScroll + vec2(0.5);
    // retail treats the map as unsigned; it is not remapped to [-1, 1]
    vec2 offset = texture(uTexture1, displacementCoord).xy * uParms[4].x;

    fragColor = texture(uTexture0, vTexCoord + offset) * vColor;
}
)";
