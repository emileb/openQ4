// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesColorMappingShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

// gles_d3 r_gamma / r_brightness -- fragment stage. The same curve as the
// desktop hardware gamma table built by R_SetColorMappings: scale by
// brightness, clamp, then raise to 1/gamma.

uniform sampler2D uTexture0;
// x = brightness, y = gamma, zw = viewport-to-texture scale
uniform vec4 uColor;

in vec2 vTexCoord;

out vec4 fragColor;

void main() {
    vec4 sampled = texture(uTexture0, vTexCoord);
    vec3 color = clamp(sampled.rgb * uColor.x, 0.0, 1.0);
    color = pow(color, vec3(1.0 / max(uColor.y, 0.001)));
    fragColor = vec4(color, sampled.a);
}
)";
