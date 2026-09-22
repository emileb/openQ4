// Copyright (C) 2026 DarkMatter Productions
//
// Shadow volume extrusion -- vertex stage.
//
// Shadow geometry is shadowCache_t, a bare idVec4 per vertex (Model.h:69),
// using the homogeneous-coordinate trick: w == 1 marks a vertex that stays
// where it is, w == 0 one that is a direction, i.e. a point at infinity.
//
// On this back end the CPU has already finished the extrusion. The back end
// reports no vertex programs, so a dynamic light's volumes come from the
// non-vertex-program turbo builder (tr_turboshadow.cpp ->
// idSIMD::CreateShadowCache), which stores each far vertex as
// (vertex - lightOrigin, 0): a direction away from the light. Static
// volumes from the classic builder carry w == 1 throughout. Either way the
// vertex is a complete homogeneous position and only needs the MVP.
//
// The ARB2 vertex program formula, w * lightOrigin + position - lightOrigin,
// belongs with the OTHER cache layout -- R_CreateVertexProgramShadowCache
// stores (vertex, 0) and leaves the subtraction to the program. Applying it
// here subtracted the light origin a second time, so every dynamic volume
// extruded away from a phantom light at twice the real one's position:
// flashlight shadows that sat still while the light moved.

#include "glsl_shaders.h"

const char * const glesStencilShadowShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

// NOTE: vec4, not vec3 -- shadow vertices are shadowCache_t, and the w
// component is the extrusion flag, not padding.
layout(location = 0) in vec4 inPosition;

uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * inPosition;
}
)";
