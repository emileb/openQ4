// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesColorMappingShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 r_gamma / r_brightness -- vertex stage. The swap-time pass that
// builtin/final_color_mapping (draw_common.cpp) is on desktop GL.
//
// One oversized triangle covering the viewport, generated from gl_VertexID so
// the draw needs no vertex buffer: (0,0), (2,0), (0,2) in texture space.

// x = brightness, y = gamma, zw = viewport-to-texture scale
uniform vec4 uColor;

out vec2 vTexCoord;

void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vTexCoord = corner * uColor.zw;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
