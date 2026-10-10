// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- shader source lookup.

	The sources themselves live one stage per file under GLES_D3/glsl/,
	following the d3es/neo/renderer/glsl convention: complete programs in raw
	string literals, `#version` and precision included, so a file can be
	pasted into a validator unchanged and a disk override
	(r_glesD3ShaderPath) is byte-identical to what ships.

	This file is only the id -> source and id -> name mapping.


===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_program.h"
#include "glsl/glsl_shaders.h"

/*
====================
R_GLESD3_EmbeddedShaderSource
====================
*/
const char *R_GLESD3_EmbeddedShaderSource( glesD3ProgramId_t id, GLenum stage ) {
	const bool vertex = ( stage == GL_VERTEX_SHADER );
	switch ( id ) {
		case GLESD3_PROGRAM_DEBUG:
			return vertex ? glesDebugShaderVP : glesDebugShaderFP;
		case GLESD3_PROGRAM_MATERIAL:
			return vertex ? glesMaterialShaderVP : glesMaterialShaderFP;
		case GLESD3_PROGRAM_INTERACTION:
			return vertex ? glesInteractionShaderVP : glesInteractionShaderFP;
		case GLESD3_PROGRAM_STENCIL_SHADOW:
			return vertex ? glesStencilShadowShaderVP : glesStencilShadowShaderFP;
		case GLESD3_PROGRAM_FOG:
			return vertex ? glesFogShaderVP : glesFogShaderFP;
		case GLESD3_PROGRAM_BLEND_LIGHT:
			return vertex ? glesBlendLightShaderVP : glesBlendLightShaderFP;
		case GLESD3_PROGRAM_CUBEMAP:
			return vertex ? glesCubeMapShaderVP : glesCubeMapShaderFP;
		case GLESD3_PROGRAM_ENVIRONMENT:
			return vertex ? glesEnvironmentShaderVP : glesEnvironmentShaderFP;
		case GLESD3_PROGRAM_BUMPY_ENVIRONMENT:
			return vertex ? glesBumpyEnvironmentShaderVP : glesBumpyEnvironmentShaderFP;
		case GLESD3_PROGRAM_HEATHAZE:
			return vertex ? glesHeatHazeShaderVP : glesHeatHazeShaderFP;
		case GLESD3_PROGRAM_HEATHAZE_MASK:
			return vertex ? glesHeatHazeShaderVP : glesHeatHazeMaskShaderFP;
		case GLESD3_PROGRAM_MONOCHROME:
			return vertex ? glesMonochromeShaderVP : glesMonochromeShaderFP;
		case GLESD3_PROGRAM_COLOR_MAPPING:
			return vertex ? glesColorMappingShaderVP : glesColorMappingShaderFP;
		case GLESD3_PROGRAM_GLSL_MULTIPLY_BLEND:
			return vertex ? glesGlslStageShaderVP : glesMultiplyBlendShaderFP;
		case GLESD3_PROGRAM_GLSL_DISPLACEMENT:
			return vertex ? glesGlslStageShaderVP : glesDisplacementShaderFP;
		case GLESD3_PROGRAM_GLSL_DISPLACEMENT_CUBE:
			return vertex ? glesDisplacementCubeShaderVP : glesDisplacementCubeShaderFP;
		case GLESD3_PROGRAM_GLSL_SNIPER_STRETCH2:
			return vertex ? glesSniperStretch2ShaderVP : glesSniperStretch2ShaderFP;
		case GLESD3_PROGRAM_SHADOW_CASTER:
		case GLESD3_PROGRAM_SHADOW_POINT_CASTER:
			return vertex ? glesShadowCasterShaderVP : glesShadowCasterShaderFP;
		case GLESD3_PROGRAM_INTERACTION_SHADOW:
		case GLESD3_PROGRAM_INTERACTION_SHADOW_POINT:
		case GLESD3_PROGRAM_INTERACTION_PHONG:
		case GLESD3_PROGRAM_INTERACTION_SHADOW_PHONG:
		case GLESD3_PROGRAM_INTERACTION_SHADOW_POINT_PHONG:
			return vertex ? glesInteractionShaderVP : glesInteractionShaderFP;
		case GLESD3_PROGRAM_FILL_LIGHT:
			return vertex ? glesFillLightShaderVP : glesFillLightShaderFP;
		default:
			return NULL;
	}
}

/*
====================
R_GLESD3_ProgramName

Doubles as the disk-override basename: r_glesD3ShaderPath looks for
<name>.vert and <name>.frag.
====================
*/
const char *R_GLESD3_ProgramName( glesD3ProgramId_t id ) {
	switch ( id ) {
		case GLESD3_PROGRAM_DEBUG:
			return "gles_d3_debug";
		case GLESD3_PROGRAM_MATERIAL:
			return "gles_d3_material";
		case GLESD3_PROGRAM_INTERACTION:
			return "gles_d3_interaction";
		case GLESD3_PROGRAM_STENCIL_SHADOW:
			return "gles_d3_stencilshadow";
		case GLESD3_PROGRAM_FOG:
			return "gles_d3_fog";
		case GLESD3_PROGRAM_BLEND_LIGHT:
			return "gles_d3_blendlight";
		case GLESD3_PROGRAM_CUBEMAP:
			return "gles_d3_cubemap";
		case GLESD3_PROGRAM_ENVIRONMENT:
			return "gles_d3_environment";
		case GLESD3_PROGRAM_BUMPY_ENVIRONMENT:
			return "gles_d3_bumpyenvironment";
		case GLESD3_PROGRAM_HEATHAZE:
			return "gles_d3_heathaze";
		case GLESD3_PROGRAM_HEATHAZE_MASK:
			return "gles_d3_heathaze_mask";
		case GLESD3_PROGRAM_MONOCHROME:
			return "gles_d3_monochrome";
		case GLESD3_PROGRAM_COLOR_MAPPING:
			return "gles_d3_colormapping";
		case GLESD3_PROGRAM_GLSL_MULTIPLY_BLEND:
			return "gles_d3_glsl_multiplyblend";
		case GLESD3_PROGRAM_GLSL_DISPLACEMENT:
			return "gles_d3_glsl_displacement";
		case GLESD3_PROGRAM_GLSL_DISPLACEMENT_CUBE:
			return "gles_d3_glsl_displacementcube";
		case GLESD3_PROGRAM_GLSL_SNIPER_STRETCH2:
			return "gles_d3_glsl_sniperstretch2";
		case GLESD3_PROGRAM_SHADOW_CASTER:
			return "gles_d3_shadowcaster";
		case GLESD3_PROGRAM_SHADOW_POINT_CASTER:
			return "gles_d3_shadowcaster_point";
		case GLESD3_PROGRAM_INTERACTION_SHADOW:
			return "gles_d3_interaction_shadow";
		case GLESD3_PROGRAM_INTERACTION_SHADOW_POINT:
			return "gles_d3_interaction_shadow_point";
		case GLESD3_PROGRAM_INTERACTION_PHONG:
			return "gles_d3_interaction_phong";
		case GLESD3_PROGRAM_INTERACTION_SHADOW_PHONG:
			return "gles_d3_interaction_shadow_phong";
		case GLESD3_PROGRAM_INTERACTION_SHADOW_POINT_PHONG:
			return "gles_d3_interaction_shadow_point_phong";
		case GLESD3_PROGRAM_FILL_LIGHT:
			return "gles_d3_filllight";
		default:
			return "unknown";
	}
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
