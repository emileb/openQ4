// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	Backend entry points the GLES module needs from the excluded legacy TUs.

	The module keeps tr_backend.cpp -- the real frame driver, with
	RB_ExecuteBackEndCommands, RB_SetBuffer, RB_SwapBuffers and the ModernGL
	executor hooks -- rather than replacing it. Measured with nm against the
	desktop build, tr_backend.cpp references exactly five symbols defined in
	the translation units this module drops (draw_common.cpp, tr_render.cpp,
	tr_rendertools.cpp, draw_arb2.cpp). Supplying those five here reuses the
	entire frame loop, where the Vulkan module had to write a 1344-line
	backend of its own.

	All five are legacy work by definition:

	  RB_DrawView                        the ARB2/fixed-function scene draw
	  RB_DrawSpecialEffects              legacy BSE effects
	  RB_ApplyResolutionScaleToBackBuffer
	  RB_ApplyCRTToBackBuffer            legacy back-buffer post
	  RB_ApplyColorMappingsToBackBuffer

	Under BE_MODERN the modern executor owns the passes it can and composites
	in R_ModernGLExecutor_ComposeVisibleFrame; nothing here needs to draw for
	the frame to reach the screen.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../ShadowMapArb2Parity.h"
#include "../GLES_D3/gles_d3_local.h"

/*
====================
RB_DrawView

Reproduces the bookkeeping of the desktop implementation (tr_render.cpp)
exactly, and stops where that one calls RB_STD_DrawView() -- the ARB2 and
fixed-function scene render, which has no meaning on an ES context.

The bookkeeping is not optional: backEnd.viewDef is read all over the shared
front-end, and currentRenderCopied / currentDepthCopied drive whether a
SS_POST_PROCESS material re-copies the screen.
====================
*/
void RB_DrawView( const void *data ) {
	const drawSurfsCommand_t *cmd = ( const drawSurfsCommand_t * )data;

	backEnd.viewDef = cmd->viewDef;

	// a new copyTexSubImage of the screen is needed when a SS_POST_PROCESS
	// material is used
	backEnd.currentRenderCopied = false;
	backEnd.currentDepthCopied = false;

	if ( !backEnd.viewDef->numDrawSurfs ) {
		return;
	}

	if ( r_skipRender.GetBool() && backEnd.viewDef->viewEntitys ) {
		return;
	}

	backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

	if ( RB_GLESD3_Active() ) {
		// gles_d3 renders the view itself, the way RB_STD_DrawView does on
		// desktop. This is the backend's only seam into the shared frame loop.
		RB_GLESD3_DrawView();
		return;
	}

	// The legacy scene render would run here. On ES the modern executor owns
	// the passes; anything it does not own is simply not drawn, which is the
	// standalone-backend contract.
}

/*
====================
RB_DrawSpecialEffects

Legacy BSE effect draw. tr_backend already skips this whenever the modern
executor owns RENDER_PASS_SPECIAL_EFFECTS.
====================
*/
void RB_DrawSpecialEffects( const void *data ) {
	( void )data;
}

/*
====================
Legacy back-buffer post

Resolution scaling, CRT emulation and colour mapping are all implemented in
draw_common.cpp against fixed-function state. They are cosmetic passes over
the finished frame; leaving them out costs those effects and nothing else.
====================
*/
void RB_ApplyResolutionScaleToBackBuffer( void ) {
}

void RB_ApplyCRTToBackBuffer( void ) {
}

void RB_ApplyColorMappingsToBackBuffer( void ) {
}

/*
===============================================================================
	Remaining legacy entry points referenced by the shared front-end.

	Found the same way as the five above: build the module and read the
	undefined-symbol closure. shared_library (rather than shared_module) is
	what makes that closure exist on darwin at all -- an MH_BUNDLE with
	-undefined dynamic_lookup would have linked silently and failed at load.

	Three groups, none of which can execute on an ES context:

	  R_ARB2_*, R_FindARBProgram, R_ValidateGLSLProgram
	      ARB assembly programs. glProgramStringARB and friends are NULL
	      pointers here, so nothing reaches these.

	  RB_*DebugLine / Polygon / Text
	      immediate-mode debug drawing from tr_rendertools.cpp.

	  RB_ShadowMap*Arb2*, RB_ResetAppleGL21RouteCounters, RB_Shutdown*
	      ARB2 shadow parity bookkeeping and teardown for resources this
	      module never creates.
===============================================================================
*/

void R_ARB2_Init( void ) {
	// The ARB2 bridge cannot exist on ES. Leaving allowARB2Path false is what
	// selects BE_MODERN in R_PickBestBackEndRenderer.
	common->Printf( "Not available: OpenGL ES has no ARB assembly programs; the modern executor renders standalone.\n" );
	glConfig.allowARB2Path = false;
}

/*
====================
R_FindARBProgram

Registers the program name and returns a non-zero handle. Returning 0 -- which
this did until 2026-08-10 -- is not a harmless stub: idMaterial only allocates
a stage's newShaderStage_t when a program handle is non-zero
(Material.cpp:2436), so a zero handle makes the parser DISCARD the fact that
the stage is a custom-program stage at all.

The stage then looks like an ordinary textured stage to every back end in this
module, and gets drawn as one. Measured on game/airdefense1: seven
`gfx/effects/energy_sparks/warp_mask` stages -- authored as heatHazeWithMask
programs sampling _currentRender -- drew as opaque black quads over the scene,
and were invisible to the unsupported-feature counters precisely because the
information had already been thrown away.

So the handle is real and the *capability* is reported false, which is the
honest split: the program exists as an authored asset, and this module has no
implementation for it yet (D7). R_IsARBProgramValid answers that question, and
the renderer skips and counts the stage instead of drawing it wrong.

Mirrors what the Vulkan module does (vk_Backend.cpp:881).
====================
*/
static const int GLES_MAX_MATERIAL_PROGRAMS = 256;
static char gles_materialProgramNames[ GLES_MAX_MATERIAL_PROGRAMS ][ MAX_OSPATH ];
static int gles_numMaterialPrograms = 0;

int R_FindARBProgram( unsigned int target, const char *program ) {
	( void )target;
	if ( program == NULL || program[0] == '\0' ) {
		return 0;
	}

	for ( int i = 0; i < gles_numMaterialPrograms; i++ ) {
		if ( idStr::Icmp( gles_materialProgramNames[ i ], program ) == 0 ) {
			return i + 1;	// 1-based: 0 means "no program" to the parser
		}
	}

	if ( gles_numMaterialPrograms >= GLES_MAX_MATERIAL_PROGRAMS ) {
		// out of slots: fall back to the old behaviour for this one rather
		// than hand back a handle that names the wrong program
		return 0;
	}

	idStr::Copynz( gles_materialProgramNames[ gles_numMaterialPrograms ], program,
			sizeof( gles_materialProgramNames[ 0 ] ) );
	gles_numMaterialPrograms++;
	return gles_numMaterialPrograms;
}

/*
====================
R_GLES_MaterialProgramName

The reverse of R_FindARBProgram, for backends in this module that implement
some of the material programs natively (GLES_D3/gles_shaderpasses.cpp, D7c).
newShaderStage_t stores only the handle, and the handle is a slot index in the
table above -- so without this the name the material author wrote is
unrecoverable at draw time, and there is nothing to dispatch on.

NULL for a handle this table never issued, which callers must treat as "not a
program we implement" rather than as an error: the parser hands out handles for
programs that exist as assets whether or not any backend can run them.
====================
*/
const char *R_GLES_MaterialProgramName( int ident ) {
	if ( ident <= 0 || ident > gles_numMaterialPrograms ) {
		return NULL;
	}
	return gles_materialProgramNames[ ident - 1 ];
}

bool R_IsARBProgramValid( unsigned int target, unsigned int ident ) {
	( void )target;
	( void )ident;
	return false;
}

void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	( void )args;
}

void R_ReportShaderPrograms_f( const idCmdArgs &args ) {
	( void )args;
}

bool R_ValidateGLSLProgram( newShaderStage_t *stage ) {
	( void )stage;
	return false;
}

void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest ) {
	( void )color; ( void )start; ( void )end; ( void )lifeTime; ( void )depthTest;
}

void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
	( void )color; ( void )winding; ( void )lifeTime; ( void )depthTest;
}

void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, const bool depthTest ) {
	( void )text; ( void )origin; ( void )scale; ( void )color; ( void )viewAxis; ( void )align; ( void )lifetime; ( void )depthTest;
}

void RB_ClearDebugLines( int time ) { ( void )time; }
void RB_ClearDebugPolygons( int time ) { ( void )time; }
void RB_ClearDebugText( int time ) { ( void )time; }
void RB_ShutdownDebugTools( void ) { }

bool RB_DrawSurfHasSoftParticleStage( const drawSurf_t *surf ) {
	( void )surf;
	return false;
}

void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] ) {
	( void )shaderRegisters;
	( void )texture;
	// identity: callers expect a usable matrix even when the stage has none
	for ( int i = 0; i < 16; i++ ) {
		matrix[i] = ( i % 5 ) == 0 ? 1.0f : 0.0f;
	}
}

void RB_ResetAppleGL21RouteCounters( void ) { }

bool RB_ShadowMapBuildArb2ParityState( const viewLight_t *vLight, const viewDef_t *viewDef, int shadowMapSize, shadowMapArb2ParityState_t &state ) {
	( void )vLight; ( void )viewDef; ( void )shadowMapSize; ( void )state;
	return false;
}

bool RB_ShadowMapResourcesKnownGood( bool pointLight ) {
	( void )pointLight;
	return false;
}

bool RB_ShadowMapTextureBindings( rendererShadowTextureBindings_t &bindings ) {
	( void )bindings;
	return false;
}

void RB_ShutdownScenePostProcess( void ) { }
void RB_ShutdownShadowMapResources( void ) { }

void RB_ResetARB2InteractionHandoffBreadcrumb( void ) { }

bool RB_ShadowMapArb2ReceiverFallbackSelfTest( void ) {
	// The self-test validates ARB2 receiver parity. Reporting success would be
	// a lie on a backend with no ARB2; reporting failure is accurate and the
	// caller treats it as "parity path unavailable".
	return false;
}

bool RB_ShadowMapEstimateArb2CacheOwnership( const viewLight_t *vLight, const viewDef_t *viewDef, shadowMapArb2CacheEstimate_t &estimate ) {
	( void )vLight; ( void )viewDef; ( void )estimate;
	return false;
}

bool RB_ShadowMapProjectedAtlasSlotForLight( int lightDefIndex, shadowMapArb2AtlasSlot_t &slot ) {
	( void )lightDefIndex; ( void )slot;
	return false;
}

void RB_ShadowMapProjectedAtlasSlotMarkUsed( int lightDefIndex ) { ( void )lightDefIndex; }

#endif /* OPENQ4_RENDERER_GLES_MODULE */
