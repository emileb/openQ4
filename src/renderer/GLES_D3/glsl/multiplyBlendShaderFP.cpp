// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesMultiplyBlendShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 glsl/MultiplyBlend.glsl -- fragment stage. Ported from
// Vulkan/shaders/material_multiply_blend.frag (retail multiplyblend.glslfp).
//
// The bullet-hole and rocket-burn decals. The material blends
// GL_DST_COLOR, GL_SRC_COLOR -- 2 x src x dst -- so a mid-grey texel leaves
// the wall untouched, darker texels darken it and brighter ones brighten it.
// As the decal fades its alpha drags the output to neutral 0.5, which is how
// it disappears without a separate fade. The glslPrograms == 0 fallback can
// only darken, which is why it reads as a flat black smudge.

uniform sampler2D uTexture0;	// Image

in vec2 vTexCoord;
in vec4 vColor;

out vec4 fragColor;

void main() {
    fragColor = texture(uTexture0, vTexCoord) * vColor.a + vec4(0.5) * (1.0 - vColor.a);
}
)";
