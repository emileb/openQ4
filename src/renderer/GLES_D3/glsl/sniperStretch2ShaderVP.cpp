// Copyright (C) 2026 DarkMatter Productions

#include "glsl_shaders.h"

const char * const glesSniperStretch2ShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h.
invariant gl_Position;

// gles_d3 glsl/SniperStretch2.glsl -- vertex stage. Ported from
// Vulkan/shaders/material_sniper_stretch2.vert (retail sniperstretch2.glslvp).
//
// uParms slot 0 is textureScale; see the fragment stage for the rest.

layout(location = 0) in vec3 inPosition;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;
uniform vec4 uParms[16];

out vec2 vTexCoord;
out vec2 vScopeTexCoord;

void main() {
    vTexCoord = inTexCoord * uParms[0].xy;
    vScopeTexCoord = inTexCoord;
    gl_Position = uMVP * vec4(inPosition, 1.0);
}
)";
