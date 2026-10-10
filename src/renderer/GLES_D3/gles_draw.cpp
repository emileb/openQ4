// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- vertex attribute binding and indexed draws.

	What tr_render.cpp supplies on the desktop path, minus the
	fixed-function halves.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

static GLuint	gles_vao = 0;
static int		gles_enabledAttributes = 0;
static bool		gles_weaponDepthRange = false;
static int		gles_weaponDepthHackDraws = 0;
static int		gles_modelDepthHackDraws = 0;
static int		gles_backEndIndexUploads = 0;

/*
====================
r_glesD3DepthHack

The weapon and model depth hacks. On by default because that is what every
other back end does; the switch exists to show what they are for -- with it
off, the view weapon pokes through any wall the player walks up to.
====================
*/
static idCVar r_glesD3DepthHack( "r_glesD3DepthHack", "1", CVAR_RENDERER | CVAR_BOOL,
		"gles_d3: apply the weapon/model depth hacks (0 lets the view weapon clip into walls)" );

/*
====================
R_GLESD3_Draw_Init

ES 3.0 keeps a usable default vertex array object, unlike a desktop core
profile, so a VAO is not strictly required here. One is used anyway: it keeps
this backend's attribute enables off the default VAO that the shared upload
and image code also touches, so neither can leave the other's state armed.
====================
*/
void R_GLESD3_Draw_Init( void ) {
	if ( gles_vao == 0 && glGenVertexArrays != NULL ) {
		glGenVertexArrays( 1, &gles_vao );
	}
	gles_enabledAttributes = 0;
}

/*
====================
R_GLESD3_Draw_Shutdown
====================
*/
void R_GLESD3_Draw_Shutdown( void ) {
	if ( gles_vao != 0 && glDeleteVertexArrays != NULL ) {
		glDeleteVertexArrays( 1, &gles_vao );
		gles_vao = 0;
	}
	gles_enabledAttributes = 0;
}

/*
====================
R_GLESD3_Draw_BeginView / EndView

Bound for the duration of a view. Restoring 0 on the way out matters: the
shared frame loop and the image code assume the default VAO.
====================
*/
void R_GLESD3_Draw_BeginView( void ) {
	if ( gles_vao != 0 ) {
		glBindVertexArray( gles_vao );
	}
	// the VAO owns the buffer bindings, and idVertexCache shadows them for the
	// legacy backend; that shadow cannot describe our VAO's state
	idVertexCache::InvalidateBufferBindings();
	gles_enabledAttributes = 0;
	R_GLESD3_InvalidateProgramState();
	glDepthRange( 0.0f, 1.0f );
	gles_weaponDepthRange = false;
	gles_weaponDepthHackDraws = 0;
	gles_modelDepthHackDraws = 0;
	gles_backEndIndexUploads = 0;
}

void R_GLESD3_Draw_EndView( void ) {
	if ( gles_vao != 0 ) {
		glBindVertexArray( 0 );
	}
	R_GLESD3_UseProgram( NULL );
	idVertexCache::InvalidateBufferBindings();
	gles_enabledAttributes = 0;
	// a view that ends on the weapon must not leave the next one (or the 2D
	// pass) squeezed into the front half of the depth range
	if ( gles_weaponDepthRange ) {
		glDepthRange( 0.0f, 1.0f );
		gles_weaponDepthRange = false;
	}
}

/*
====================
R_GLESD3_SpaceMVP

RB_EnterWeaponDepthHack / RB_EnterModelDepthHack (tr_render.cpp:317, :332),
which this module excludes, folded into the one place every pass builds a
surface's MVP. Without it the view weapon is drawn at its true depth, so it
sinks into any wall the player stands against and its muzzle effects with it.

The weapon hack is two halves and both are needed: the projection's z term is
scaled so the weapon sits in front of the world, and the depth range is halved
so it can never be behind it. The range is tracked rather than set per draw,
because every pass calls this for every surface.

cl_gunfov's weapon-FOV refit is not carried, matching the Vulkan module's
VK_BuildSurfMVP: it is 0 (follow the view FOV) unless a player sets it.
====================
*/
void R_GLESD3_SpaceMVP( const viewEntity_t *space, float mvp[16] ) {
	float projection[ 16 ];
	memcpy( projection, backEnd.viewDef->projectionMatrix, sizeof( projection ) );

	bool weaponRange = false;
	if ( r_glesD3DepthHack.GetBool() ) {
		if ( space->modelDepthHack != 0.0f ) {
			projection[ 14 ] -= space->modelDepthHack;
			gles_modelDepthHackDraws++;
		} else if ( space->weaponDepthHack ) {
			projection[ 14 ] *= 0.25f;
			weaponRange = true;
			gles_weaponDepthHackDraws++;
		}
	}
	if ( weaponRange != gles_weaponDepthRange ) {
		gles_weaponDepthRange = weaponRange;
		glDepthRange( 0.0f, weaponRange ? 0.5f : 1.0f );
	}

	myGlMultMatrix( space->modelViewMatrix, projection, mvp );
}

// MVP builds in the current view that took each hack, for r_glesD3Report
void R_GLESD3_DepthHackCounts( int &weaponDraws, int &modelDraws ) {
	weaponDraws = gles_weaponDepthHackDraws;
	modelDraws = gles_modelDepthHackDraws;
}

// back-end CPU-index uploads in the current view, for r_glesD3Report
int R_GLESD3_BackEndIndexUploads( void ) {
	return gles_backEndIndexUploads;
}

/*
====================
R_GLESD3_EnableAttributes

Enables exactly the requested set and disables the rest. A left-over enabled
attribute pointing at a freed vertex-cache range is a crash on some drivers
and silent garbage on others, so the disable half is not optional.
====================
*/
static void R_GLESD3_EnableAttributes( int mask ) {
	for ( int i = 0; i < GLESD3_ATTR_COUNT; i++ ) {
		const int bit = 1 << i;
		if ( ( mask & bit ) == ( gles_enabledAttributes & bit ) ) {
			continue;
		}
		if ( mask & bit ) {
			glEnableVertexAttribArray( i );
		} else {
			glDisableVertexAttribArray( i );
		}
	}
	gles_enabledAttributes = mask;
}

/*
====================
R_GLESD3_EnableAttributesForShadow

Shadow volumes carry position only, as a four-component shadowCache_t. The
other five attributes must be OFF: leaving them enabled points them at a
16-byte-stride buffer that has no colour, normal or texcoord to read.
====================
*/
void R_GLESD3_EnableAttributesForShadow( void ) {
	R_GLESD3_EnableAttributes( 1 << GLESD3_ATTR_POSITION );
}

// For draws whose vertices come from gl_VertexID alone, like the fullscreen
// colour-mapping triangle.
void R_GLESD3_DisableAllAttributes( void ) {
	R_GLESD3_EnableAttributes( 0 );
}

/*
====================
R_GLESD3_InvalidateAttributeState

Forces the next bind to re-issue every attribute. Needed after a pass that
bound a different vertex layout -- the shadow pass leaves position as four
components with a 16-byte stride, which would be read as garbage against
idDrawVert.
====================
*/
void R_GLESD3_InvalidateAttributeState( void ) {
	gles_enabledAttributes = 0;
	for ( int i = 0; i < GLESD3_ATTR_COUNT; i++ ) {
		glDisableVertexAttribArray( i );
	}
}

/*
====================
R_GLESD3_BindDrawVertAttributes

Points the full attribute set at one interleaved idDrawVert range.

idVertexCache::Position() binds the owning buffer object as a side effect and
returns a byte offset into it, so the bind and the offsets cannot disagree.
Attribute addresses are formed with RB_DrawVertAttributePointer because offset
zero must stay an integer offset rather than becoming null pointer arithmetic.

Returns false when the geometry has no usable vertex buffer, which under
BE_GLES_D3 means the surface is simply not drawn -- there is no legacy path
behind this one to catch it.
====================
*/
bool R_GLESD3_BindDrawVertAttributes( const srfTriangles_t *tri ) {
	if ( tri == NULL || tri->ambientCache == NULL ) {
		return false;
	}

	const void *base = vertexCache.Position( tri->ambientCache );
	const GLsizei stride = sizeof( idDrawVert );

	R_GLESD3_EnableAttributes( ( 1 << GLESD3_ATTR_POSITION )
			| ( 1 << GLESD3_ATTR_COLOR )
			| ( 1 << GLESD3_ATTR_NORMAL )
			| ( 1 << GLESD3_ATTR_TANGENT )
			| ( 1 << GLESD3_ATTR_BITANGENT )
			| ( 1 << GLESD3_ATTR_TEXCOORD ) );

	glVertexAttribPointer( GLESD3_ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, stride,
			RB_DrawVertAttributePointer( base, offsetof( idDrawVert, xyz ) ) );
	// normalized: the colour is stored as four bytes and the shaders expect 0..1
	glVertexAttribPointer( GLESD3_ATTR_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
			RB_DrawVertAttributePointer( base, offsetof( idDrawVert, color ) ) );
	glVertexAttribPointer( GLESD3_ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, stride,
			RB_DrawVertAttributePointer( base, offsetof( idDrawVert, normal ) ) );
	glVertexAttribPointer( GLESD3_ATTR_TANGENT, 3, GL_FLOAT, GL_FALSE, stride,
			RB_DrawVertAttributePointer( base, offsetof( idDrawVert, tangents[0] ) ) );
	glVertexAttribPointer( GLESD3_ATTR_BITANGENT, 3, GL_FLOAT, GL_FALSE, stride,
			RB_DrawVertAttributePointer( base, offsetof( idDrawVert, tangents[1] ) ) );
	glVertexAttribPointer( GLESD3_ATTR_TEXCOORD, 2, GL_FLOAT, GL_FALSE, stride,
			RB_DrawVertAttributePointer( base, offsetof( idDrawVert, st ) ) );

	return true;
}

/*
====================
R_GLESD3_BindCubeTexDirAttribute

Points GLESD3_ATTR_TEXDIR at a cube-map texgen direction, on top of a binding
already made by R_GLESD3_BindDrawVertAttributes.

Two sources, which is the whole reason this is separate from the idDrawVert
binder:

  dynamicTexCoords != NULL   the front end's skybox/wobblesky stream
                             (R_SkyboxTexGen, tr_light.cpp:517) -- a DIFFERENT
                             buffer object, tightly packed vec3, stride 12.
                             vertexCache.Position() rebinds GL_ARRAY_BUFFER as
                             a side effect, and glVertexAttribPointer captures
                             whatever is bound at the time, so the two
                             attribute sets can come from two buffers and this
                             must run second.
  otherwise                  the idDrawVert normal, for TG_DIFFUSE_CUBE.

Returns false when a skybox stage arrived with no stream, which means the
front end ran out of frame-temp vertex cache; the surface is then not drawn
rather than drawn with a stale direction.
====================
*/
bool R_GLESD3_BindCubeTexDirAttribute( const drawSurf_t *surf, bool fromNormal ) {
	if ( fromNormal ) {
		const srfTriangles_t *tri = surf->geo;
		if ( tri == NULL || tri->ambientCache == NULL ) {
			return false;
		}
		const void *base = vertexCache.Position( tri->ambientCache );
		R_GLESD3_EnableAttributes( gles_enabledAttributes | ( 1 << GLESD3_ATTR_TEXDIR ) );
		glVertexAttribPointer( GLESD3_ATTR_TEXDIR, 3, GL_FLOAT, GL_FALSE, sizeof( idDrawVert ),
				RB_DrawVertAttributePointer( base, offsetof( idDrawVert, normal ) ) );
		return true;
	}

	if ( surf->dynamicTexCoords == NULL ) {
		return false;
	}
	const void *base = vertexCache.Position( surf->dynamicTexCoords );
	R_GLESD3_EnableAttributes( gles_enabledAttributes | ( 1 << GLESD3_ATTR_TEXDIR ) );
	glVertexAttribPointer( GLESD3_ATTR_TEXDIR, 3, GL_FLOAT, GL_FALSE, sizeof( idVec3 ),
			RB_DrawVertAttributePointer( base, 0 ) );
	return true;
}

/*
====================
R_GLESD3_DisableCubeTexDirAttribute

Left enabled, the direction attribute points at a frame-temp range that the
next frame reuses for something else -- and it is the only attribute in this
backend whose buffer is not the surface's own ambient cache, so it cannot be
fixed up by the next R_GLESD3_BindDrawVertAttributes call.
====================
*/
void R_GLESD3_DisableCubeTexDirAttribute( void ) {
	R_GLESD3_EnableAttributes( gles_enabledAttributes & ~( 1 << GLESD3_ATTR_TEXDIR ) );
}

/*
====================
R_GLESD3_DrawElements

Client-memory index pointers are legal in a compatibility context and REMOVED
in ES: without an index buffer every indexed draw fails with
GL_INVALID_VALUE and the target reads back as zero. That cost a full round in
the ModernGL bring-up (G6c-G6e), where r_useIndexBuffers also defaults to 0.
So there is no client-memory fallback here -- a surface with no index cache is
not drawn, and says so once.
====================
*/
vertCache_t *R_GLESD3_EnsureIndexCache( const srfTriangles_t *tri ) {
	if ( tri == NULL || tri->numIndexes <= 0 ) {
		return NULL;
	}
	if ( tri->indexCache != NULL ) {
		return tri->indexCache;
	}
	if ( tri->indexes == NULL ) {
		return NULL;
	}
	// Each of these is an upload in the middle of the back end, into a buffer
	// the GPU is already reading -- on Adreno a stall per draw (4b0f549f).
	// r_glesD3Report counts them; geometry that lands here every frame wants a
	// front-end index cache instead.
	gles_backEndIndexUploads++;
	// A frame temp, not a write-back into tri->indexCache: some of the geometry
	// that lands here is persistent (light interaction tris are owned by the
	// interaction cache) and a frame-temp stored on it would dangle next frame.
	return vertexCache.AllocFrameTemp( tri->indexes,
			tri->numIndexes * sizeof( tri->indexes[0] ), true );
}

bool R_GLESD3_DrawElements( const srfTriangles_t *tri ) {
	if ( tri == NULL || tri->numIndexes <= 0 ) {
		return false;
	}
	vertCache_t *indexCache = R_GLESD3_EnsureIndexCache( tri );
	if ( indexCache == NULL ) {
		static bool reported = false;
		if ( !reported ) {
			reported = true;
			common->Warning( "gles_d3: geometry with neither an index cache nor CPU indexes "
					"cannot be drawn on ES (client-memory index pointers do not exist)" );
		}
		return false;
	}

	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += tri->numIndexes;
	backEnd.pc.c_drawVertexes += tri->numVerts;
	backEnd.pc.c_vboIndexes += tri->numIndexes;

	glDrawElements( GL_TRIANGLES,
			r_singleTriangle.GetBool() ? 3 : tri->numIndexes,
			GL_INDEX_TYPE,
			vertexCache.Position( indexCache ) );
	return true;
}

/*
====================
R_GLESD3_AlphaTestReference

GL_State folds the alpha test into GLS_ATEST_BITS and hands them to
glAlphaFunc, which is a no-op stub on ES (GLES/gles_GLStubs.cpp). The test has
to move into the fragment shader, so the state bits are translated into a
reference value the shaders compare against with `discard`.

Returns < 0 when no test is armed. GLS_ATEST_EQ_255 is expressed as a
reference just under 1.0 rather than an equality: the byte 255 arrives as
exactly 1.0 after normalization, and every lesser value is below the
reference, so `a <= ref -> discard` reproduces `EQUAL 1` for real data
without needing a second comparison mode in every shader.

stateBits is passed in rather than read from backEnd.glState because the two
are only the same once GL_State has run for this draw. A caller that writes
its uniforms before its GL_State -- which the cube texgen path did -- would
otherwise arm the PREVIOUS stage's alpha test. Callers with a stage in hand
should pass pStage->drawStateBits; the ones that genuinely mean "whatever is
current" pass backEnd.glState.glStateBits explicitly.
====================
*/
float R_GLESD3_AlphaTestReference( int stateBits ) {
	switch ( stateBits & GLS_ATEST_BITS ) {
		case GLS_ATEST_EQ_255:
			return 0.996f;
		case GLS_ATEST_LT_128:
			// LESS 0.5 keeps fragments BELOW the reference, the opposite sense
			// of the other two. The shaders only implement "discard at or
			// below", so this one is handled by its caller; treat as no test
			// here rather than silently inverting it.
			return -1.0f;
		case GLS_ATEST_GE_128:
			return 0.5f;
		default:
			return -1.0f;
	}
}

/*
====================
R_GLESD3_SetupProgramForDraw

The uniforms every program shares. Written on every draw, identity included:
GL zero-initialises unset uniforms, and a zero matrix collapses every texture
coordinate onto one texel -- the exact defect the ModernGL path hit with
stage texture matrices (G6l).
====================
*/
void R_GLESD3_SetupProgramForDraw( const glesProgram_t *program, const float mvp[16],
		const idVec4 &color ) {
	if ( program == NULL ) {
		return;
	}
	R_GLESD3_UseProgram( program );

	if ( program->uMVP >= 0 ) {
		glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );
	}
	if ( program->uColor >= 0 ) {
		glUniform4f( program->uColor, color.x, color.y, color.z, color.w );
	}
	if ( program->uAlphaTest >= 0 ) {
		// no stage in hand here: this is the shared setup for draws whose
		// GL_State has already run
		glUniform1f( program->uAlphaTest,
				R_GLESD3_AlphaTestReference( backEnd.glState.glStateBits ) );
	}
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
