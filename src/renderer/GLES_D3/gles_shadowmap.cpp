// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- shadow maps.

	A lean port of the Vulkan module's vk_ShadowMap.cpp: one map per light
	pass, rendered fresh every view, no atlas, cache, cascades, PCSS or
	translucent moments. Projected, spot and parallel lights render a 2D
	depth map through the shared ShadowMapProjected clip planes; point lights
	render a depth cube with ARB2's face axes. Both store LINEAR light depth
	from the fragment stage, so the bias cvars mean what they mean on the other
	back ends.

	Stencil shadows are not replaced, they stay the fallback. This back end
	keeps RB_ShadowMapResourcesKnownGood false, so the front end goes on
	building every stencil volume, and any light or receiver pass the maps
	cannot serve this view -- an incomplete caster set, an unsupported
	caster, a subview, a full pool -- is drawn with stencil exactly as before.
	r_useShadowMap 0 (the default) leaves this file inert.

	Receiver ownership mirrors the stencil chain crossing in
	gles_interaction.cpp. LOCAL receivers (localInteractions) are shadowed by
	the global casters only; GLOBAL receivers (globalInteractions and the
	translucent chain) by global and local casters. A light without local
	casters therefore needs one map for both passes.

	Every map is rendered at the start of the view, before the depth prepass,
	so a tile-based GPU switches away from the scene target once per view
	rather than once per light.

===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../ShadowMapProjected.h"
#include "gles_d3_local.h"
#include "gles_program.h"

/*
====================
r_glesD3ShadowMapMaxLights

How many lights per view get a map. The pool behind it is sized to this many
2D maps and this many cubes, created on first use: cube memory is
6 x r_shadowMapPointSize^2 x 4 bytes each, so 8 cubes at 512 is 48 MB. Lights
past the cap, smallest on screen first, keep stencil shadows.
====================
*/
static idCVar r_glesD3ShadowMapMaxLights( "r_glesD3ShadowMapMaxLights", "8", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
		"gles_d3: lights per view that get a shadow map with r_useShadowMap 1; the rest keep stencil shadows", 0, 32 );

static const int GLESD3_SHADOW_MAX_LIGHTS = 64;
static const int GLESD3_SHADOW_MAX_SLOTS = 32;
static const int GLESD3_SHADOW_PASS_LOCAL = 0;
static const int GLESD3_SHADOW_PASS_GLOBAL = 1;
static const float GLESD3_POINT_SHADOW_NEAR = 0.01f;

typedef struct {
	GLuint		texture;
	int			size;
} glesShadowSlot_t;

typedef struct {
	const viewLight_t *		vLight;
	bool					point;
	int						size;
	float					pointFar;
	shadowMapProjectedLightState_t projected;
	// per pass: the slot index into the 2D or cube pool, -1 for stencil, or
	// GLESD3_SHADOW_SLOT_UNSHADOWED when the pass has no casters at all
	int						slot[ 2 ];
	float					bias[ 4 ];
	float					filter[ 4 ];
} glesShadowLight_t;

static const int GLESD3_SHADOW_SLOT_UNSHADOWED = -2;

static glesShadowSlot_t		gles_shadow2D[ GLESD3_SHADOW_MAX_SLOTS ];
static glesShadowSlot_t		gles_shadowCube[ GLESD3_SHADOW_MAX_SLOTS ];
static GLuint				gles_shadowFbo = 0;
// allocation failures are logged once and retried next view rather than
// disabling maps for the session (see GLESD3_ShadowEnsureSlot)
static bool					gles_shadowAllocationWarned = false;

static glesShadowLight_t	gles_shadowLights[ GLESD3_SHADOW_MAX_LIGHTS ];
static int					gles_numShadowLights = 0;
static const viewDef_t *	gles_shadowViewDef = NULL;

// the receiver pass the interaction chain is in, set by RB_GLESD3_ShadowMapBeginPass
static const glesShadowLight_t *	gles_activeShadowLight = NULL;

// r_glesD3Report counters for the current view
static int gles_shadowMappedPasses = 0;
static int gles_shadowStencilPasses = 0;
static int gles_shadowCasterDraws = 0;

/*
===============================================================================
	Resources
===============================================================================
*/

static void GLESD3_ShadowDeleteSlot( glesShadowSlot_t &slot ) {
	if ( slot.texture != 0 ) {
		glDeleteTextures( 1, &slot.texture );
	}
	slot.texture = 0;
	slot.size = 0;
}

/*
====================
RB_GLESD3_ShutdownShadowMaps

RB_ShutdownShadowMapResources, called at vid_restart before the image purge.
====================
*/
void RB_GLESD3_ShutdownShadowMaps( void ) {
	for ( int i = 0; i < GLESD3_SHADOW_MAX_SLOTS; i++ ) {
		GLESD3_ShadowDeleteSlot( gles_shadow2D[i] );
		GLESD3_ShadowDeleteSlot( gles_shadowCube[i] );
	}
	if ( gles_shadowFbo != 0 ) {
		glDeleteFramebuffers( 1, &gles_shadowFbo );
		gles_shadowFbo = 0;
	}
	gles_shadowAllocationWarned = false;
	gles_numShadowLights = 0;
	gles_shadowViewDef = NULL;
}

static void GLESD3_ShadowSetSamplerParams( GLenum target ) {
	glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	if ( target == GL_TEXTURE_CUBE_MAP ) {
		glTexParameteri( target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
	}
	// a depth texture is only filterable with comparison on in ES 3.0, and
	// LINEAR then gives a 2x2 PCF per tap for free
	glTexParameteri( target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE );
	glTexParameteri( target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL );
}

/*
====================
GLESD3_ShadowEnsureSlot

Creates (or resizes) one pool texture. Bound through unit 6 with the
direct-access helper so the TMU tracker stays in step.
====================
*/
static bool GLESD3_ShadowEnsureSlot( glesShadowSlot_t &slot, bool cube, int size ) {
	if ( slot.texture != 0 && slot.size == size ) {
		return true;
	}
	GLESD3_ShadowDeleteSlot( slot );

	const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
	// GL keeps one pending flag per error kind, and the engine routinely
	// arrives here with one set (r_glesD3Report shows entryErr=0x0500 at view
	// start). Draining a single call left a stale INVALID_OPERATION to be
	// blamed on this allocation, which used to switch shadow maps off for the
	// whole session -- seen in an interactive run that went through the menu
	// first. Drain them all.
	for ( int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++ ) {
	}
	glGenTextures( 1, &slot.texture );
	GL_SelectTexture( 6 );
	R_BindTextureForDirectAccess( target, slot.texture );
	if ( cube ) {
		for ( int face = 0; face < 6; face++ ) {
			glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT24, size, size, 0,
					GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL );
		}
	} else {
		glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
				GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL );
	}
	GLESD3_ShadowSetSamplerParams( target );
	R_BindTextureForDirectAccess( target, 0 );
	GL_SelectTexture( 0 );

	const GLenum err = glGetError();
	if ( err != GL_NO_ERROR ) {
		if ( !gles_shadowAllocationWarned ) {
			gles_shadowAllocationWarned = true;
			common->Warning( "gles_d3: %s shadow map %ix%i allocation raised 0x%04x; "
					"those lights keep stencil shadows this view and it is retried",
					cube ? "cube" : "2D", size, size, err );
		}
		GLESD3_ShadowDeleteSlot( slot );
		return false;
	}
	slot.size = size;
	return true;
}

/*
====================
GLESD3_ShadowAttach

Points the shared depth-only FBO at one 2D map or one cube face and checks it.
====================
*/
static bool GLESD3_ShadowAttach( GLenum textarget, GLuint texture ) {
	if ( gles_shadowFbo == 0 ) {
		glGenFramebuffers( 1, &gles_shadowFbo );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, gles_shadowFbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, textarget, texture, 0 );
	const GLenum none = GL_NONE;
	glDrawBuffers( 1, &none );
	glReadBuffer( GL_NONE );
	const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( status != GL_FRAMEBUFFER_COMPLETE ) {
		static bool reported = false;
		if ( !reported ) {
			reported = true;
			common->Warning( "gles_d3: shadow map framebuffer incomplete (0x%04x); shadows stay stencil", status );
		}
		return false;
	}
	return true;
}

/*
===============================================================================
	Per-view selection
===============================================================================
*/

/*
====================
GLESD3_ShadowResolveCasterGeo

VK_ShadowMap_ResolveCasterGeo: the caster's own ambient surface, with an
ambient cache made on demand. Packed MD5R batches have no idDrawVert stream on
this back end and make the pass unrepresentable.
====================
*/
static srfTriangles_t *GLESD3_ShadowResolveCasterGeo( const drawSurf_t *surf ) {
	if ( surf->geo == NULL || surf->space == NULL ) {
		return NULL;
	}
	srfTriangles_t *geo = surf->geo->ambientSurface != NULL
			? surf->geo->ambientSurface : const_cast<srfTriangles_t *>( surf->geo );
	if ( geo == NULL || geo->numVerts <= 0 || geo->numIndexes <= 0 || R_TriHasPrimBatchMesh( geo ) ) {
		return NULL;
	}
	if ( geo->ambientCache == NULL ) {
		if ( geo->verts == NULL || !R_CreateAmbientCache( geo, false ) ) {
			return NULL;
		}
	}
	if ( geo->indexes == NULL && geo->indexCache == NULL ) {
		return NULL;
	}
	return geo;
}

/*
====================
GLESD3_ShadowPerforatedSupported

VK_ShadowMap_PerforatedCasterSupported: every active alpha-tested stage must
be a plain static image with explicit texcoords and a LESS/EQUAL/GREATER test.
====================
*/
static bool GLESD3_ShadowPerforatedSupported( const drawSurf_t *surf ) {
	if ( surf->material == NULL || surf->shaderRegisters == NULL ) {
		return false;
	}
	const float *regs = surf->shaderRegisters;
	const int stageCount = surf->material->GetNumStages();
	for ( int i = 0; i < stageCount; i++ ) {
		const shaderStage_t *pStage = surf->material->GetStage( i );
		if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}
		if ( regs[ pStage->color.registers[3] ] <= 0.0f ) {
			continue;
		}
		if ( pStage->texture.cinematic != NULL || pStage->texture.dynamic != DI_STATIC
				|| pStage->texture.image == NULL || pStage->texture.texgen != TG_EXPLICIT ) {
			return false;
		}
		if ( pStage->alphaTestMode != GL_LESS && pStage->alphaTestMode != GL_EQUAL
				&& pStage->alphaTestMode != GL_GREATER ) {
			return false;
		}
	}
	return true;
}

static bool GLESD3_ShadowChainRepresentable( const drawSurf_t *chain, int &count ) {
	for ( const drawSurf_t *surf = chain; surf != NULL; surf = surf->nextOnLight ) {
		if ( GLESD3_ShadowResolveCasterGeo( surf ) == NULL ) {
			return false;
		}
		if ( surf->material != NULL && surf->material->Coverage() == MC_PERFORATED
				&& !GLESD3_ShadowPerforatedSupported( surf ) ) {
			return false;
		}
		count++;
	}
	return true;
}

/*
====================
GLESD3_ShadowPassRepresentable

Whether a receiver pass can be served by a map at all, and how many casters
that map holds. LOCAL is the global casters; GLOBAL adds the local ones.
====================
*/
static bool GLESD3_ShadowPassRepresentable( const viewLight_t *vLight, int pass, int &casterCount ) {
	casterCount = 0;
	const int bit = ( pass == GLESD3_SHADOW_PASS_LOCAL )
			? SHADOWMAP_RECEIVER_MASK_LOCAL : SHADOWMAP_RECEIVER_MASK_GLOBAL;
	// a caster the map does not hold would need a stencil supplement; this
	// lean port takes full stencil for that pass instead
	if ( vLight->shadowMapIncompleteMapMask & bit ) {
		return false;
	}
	if ( !GLESD3_ShadowChainRepresentable( vLight->globalShadowMapCasters, casterCount )
			|| !GLESD3_ShadowChainRepresentable( vLight->globalShadowMapDynamicCasters, casterCount ) ) {
		return false;
	}
	if ( pass == GLESD3_SHADOW_PASS_GLOBAL ) {
		if ( !GLESD3_ShadowChainRepresentable( vLight->localShadowMapCasters, casterCount )
				|| !GLESD3_ShadowChainRepresentable( vLight->localShadowMapDynamicCasters, casterCount ) ) {
			return false;
		}
	}
	return true;
}

static bool GLESD3_ShadowLightEligible( const viewLight_t *vLight ) {
	const idMaterial *shader = vLight->lightShader;
	if ( shader == NULL || shader->IsFogLight() || shader->IsBlendLight()
			|| shader->IsAmbientLight() || !shader->LightCastsShadows() ) {
		return false;
	}
	// the policy gate the Vulkan module applies (RB_ShadowMapLightPolicySupportReason)
	if ( vLight->lightDef != NULL
			&& ( vLight->lightDef->parms.noShadows || vLight->lightDef->parms.noDynamicShadows ) ) {
		return false;
	}
	if ( vLight->pointLight && !vLight->parallel && !r_shadowMapPointLights.GetBool() ) {
		return false;
	}
	// translucent moments move blended casters out of the opaque chains into
	// lists this port does not draw; a map without them would be incomplete
	if ( vLight->globalTranslucentShadowMapCasters != NULL
			|| vLight->localTranslucentShadowMapCasters != NULL ) {
		return false;
	}
	return vLight->localInteractions != NULL || vLight->globalInteractions != NULL
			|| ( r_shadowMapTranslucentReceivers.GetBool() && vLight->translucentInteractions != NULL );
}

/*
====================
GLESD3_ShadowAreaSort

Largest on-screen light first, so the cap spends maps where they show.
====================
*/
static int GLESD3_ShadowScissorArea( const viewLight_t *vLight ) {
	const idScreenRect &r = vLight->scissorRect;
	if ( r.IsEmpty() ) {
		return 0;
	}
	return ( r.x2 - r.x1 + 1 ) * ( r.y2 - r.y1 + 1 );
}

/*
===============================================================================
	Caster rendering
===============================================================================
*/

/*
====================
GLESD3_ShadowApplyCasterCull

RB_ShadowMapApplyCasterCull (draw_arb2.cpp:3042). Casters render in light
space, so GL_Cull's mirror flip must not apply: the face is set directly.
idTech4 winding makes the engine-front side the GL_BACK face, so culling
GL_FRONT stores the light-facing near shell; AUTO draws two-sided whenever
that is not provably safe.
====================
*/
static int gles_shadowCullApplied = -1;

static void GLESD3_ShadowApplyCasterCull( const viewLight_t *vLight, const drawSurf_t *surf,
		const srfTriangles_t *geo ) {
	const int mode = idMath::ClampInt( 0, 2, r_shadowMapCasterCulling.GetInteger() );
	const int materialCull = ( surf->material != NULL ) ? surf->material->GetCullType() : CT_FRONT_SIDED;
	const float *modelMatrix = surf->space->modelMatrix;
	const bool automaticTwoSided = mode == 2
			&& ( !geo->perfectHull
				|| R_ShadowMapCasterTransformNeedsTwoSided( modelMatrix )
				|| R_ShadowMapLightOriginInsideCasterBounds( vLight, modelMatrix,
					geo->bounds[0].ToFloatPtr(), geo->bounds[1].ToFloatPtr() ) );
	int desired = 0;
	if ( mode != 0 && !automaticTwoSided && materialCull != CT_TWO_SIDED ) {
		desired = ( materialCull == CT_BACK_SIDED ) ? GL_BACK : GL_FRONT;
	}
	if ( desired == gles_shadowCullApplied ) {
		return;
	}
	gles_shadowCullApplied = desired;
	if ( desired == 0 ) {
		glDisable( GL_CULL_FACE );
	} else {
		glEnable( GL_CULL_FACE );
		glCullFace( (GLenum)desired );
	}
}

/*
====================
GLESD3_ShadowDrawCaster

One caster surface into the bound map. lightMatrix is the light's world ->
clip (projected) or world -> face view (point) matrix; depthRow carries the
projected falloff plane in world space, or the point GL perspective terms.
====================
*/
static void GLESD3_ShadowDrawCaster( const viewLight_t *vLight, const drawSurf_t *surf, bool point,
		const float lightMatrix[16], const idPlane &worldDepthPlane, const float pointTerms[4],
		const float casterOffset[4], int cubeFace ) {
	srfTriangles_t *geo = GLESD3_ShadowResolveCasterGeo( surf );
	if ( geo == NULL ) {
		return;
	}
	const float *modelMatrix = surf->space->modelMatrix;
	if ( point && R_ShadowMapCasterOutsidePointFace( vLight, modelMatrix,
			geo->bounds[0].ToFloatPtr(), geo->bounds[1].ToFloatPtr(), cubeFace ) ) {
		return;
	}

	float mvp[16];
	myGlMultMatrix( modelMatrix, lightMatrix, mvp );
	float depthRow[4];
	if ( point ) {
		memcpy( depthRow, pointTerms, sizeof( depthRow ) );
	} else {
		idPlane local;
		R_GlobalPlaneToLocal( modelMatrix, worldDepthPlane, local );
		depthRow[0] = local[0];
		depthRow[1] = local[1];
		depthRow[2] = local[2];
		depthRow[3] = local[3];
	}

	if ( !R_GLESD3_BindDrawVertAttributes( geo ) ) {
		return;
	}
	GLESD3_ShadowApplyCasterCull( vLight, surf, geo );

	const glesD3ProgramId_t id = point ? GLESD3_PROGRAM_SHADOW_POINT_CASTER : GLESD3_PROGRAM_SHADOW_CASTER;
	const bool perforated = surf->material != NULL && surf->material->Coverage() == MC_PERFORATED;
	bool drewStage = false;

	if ( perforated ) {
		const glesProgram_t *program = R_GLESD3_Program( id, GLESD3_VARIANT_ALPHATEST );
		const float *regs = surf->shaderRegisters;
		const int stageCount = surf->material->GetNumStages();
		for ( int i = 0; program != NULL && i < stageCount; i++ ) {
			const shaderStage_t *pStage = surf->material->GetStage( i );
			if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}
			drewStage = true;	// an active stage with zero alpha scale emits no depth
			const float alphaScale = regs[ pStage->color.registers[3] ];
			if ( alphaScale <= 0.0f ) {
				continue;
			}
			idVec4 matrixS( 1.0f, 0.0f, 0.0f, 0.0f );
			idVec4 matrixT( 0.0f, 1.0f, 0.0f, 0.0f );
			if ( pStage->texture.hasMatrix ) {
				matrixS.Set( regs[ pStage->texture.matrix[0][0] ], regs[ pStage->texture.matrix[0][1] ],
						0.0f, regs[ pStage->texture.matrix[0][2] ] );
				matrixT.Set( regs[ pStage->texture.matrix[1][0] ], regs[ pStage->texture.matrix[1][1] ],
						0.0f, regs[ pStage->texture.matrix[1][2] ] );
			}
			float offset[4];
			memcpy( offset, casterOffset, sizeof( offset ) );
			offset[3] = ( pStage->alphaTestMode == GL_LESS ) ? -1.0f
					: ( pStage->alphaTestMode == GL_EQUAL ? 2.0f : 1.0f );
			const float color[4] = { 1.0f, 1.0f, 1.0f, alphaScale };

			R_GLESD3_UseProgram( program );
			glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );
			glUniform4fv( program->uShadowDepthRow, 1, depthRow );
			glUniform4fv( program->uShadowCasterOffset, 1, offset );
			glUniform4fv( program->uTexMatrixS, 1, matrixS.ToFloatPtr() );
			glUniform4fv( program->uTexMatrixT, 1, matrixT.ToFloatPtr() );
			glUniform4fv( program->uColor, 1, color );
			glUniform1f( program->uAlphaTest, regs[ pStage->alphaTestRegister ] );
			GL_SelectTexture( 0 );
			pStage->texture.image->Bind();
			R_GLESD3_DrawElements( geo );
			gles_shadowCasterDraws++;
		}
	}

	if ( !drewStage ) {
		const glesProgram_t *program = R_GLESD3_Program( id );
		if ( program == NULL ) {
			return;
		}
		R_GLESD3_UseProgram( program );
		glUniformMatrix4fv( program->uMVP, 1, GL_FALSE, mvp );
		glUniform4fv( program->uShadowDepthRow, 1, depthRow );
		glUniform4fv( program->uShadowCasterOffset, 1, casterOffset );
		R_GLESD3_DrawElements( geo );
		gles_shadowCasterDraws++;
	}
}

static void GLESD3_ShadowDrawChain( const viewLight_t *vLight, const drawSurf_t *chain, bool point,
		const float lightMatrix[16], const idPlane &worldDepthPlane, const float pointTerms[4],
		const float casterOffset[4], int cubeFace ) {
	for ( const drawSurf_t *surf = chain; surf != NULL; surf = surf->nextOnLight ) {
		GLESD3_ShadowDrawCaster( vLight, surf, point, lightMatrix, worldDepthPlane, pointTerms,
				casterOffset, cubeFace );
	}
}

/*
====================
GLESD3_ShadowDrawPassCasters

The casters a pass's map holds: LOCAL is the global casters, GLOBAL adds the
local ones (VK_ShadowMap_DrawPassCasters).
====================
*/
static void GLESD3_ShadowDrawPassCasters( const viewLight_t *vLight, int pass, bool point,
		const float lightMatrix[16], const idPlane &worldDepthPlane, const float pointTerms[4],
		const float casterOffset[4], int cubeFace ) {
	GLESD3_ShadowDrawChain( vLight, vLight->globalShadowMapCasters, point, lightMatrix, worldDepthPlane,
			pointTerms, casterOffset, cubeFace );
	GLESD3_ShadowDrawChain( vLight, vLight->globalShadowMapDynamicCasters, point, lightMatrix, worldDepthPlane,
			pointTerms, casterOffset, cubeFace );
	if ( pass == GLESD3_SHADOW_PASS_GLOBAL ) {
		GLESD3_ShadowDrawChain( vLight, vLight->localShadowMapCasters, point, lightMatrix, worldDepthPlane,
				pointTerms, casterOffset, cubeFace );
		GLESD3_ShadowDrawChain( vLight, vLight->localShadowMapDynamicCasters, point, lightMatrix, worldDepthPlane,
				pointTerms, casterOffset, cubeFace );
	}
}

/*
====================
GLESD3_ShadowPointFaceView

RB_PointShadowMapBuildViewAxis + RB_PointShadowMapBuildModelViewMatrix
(draw_arb2.cpp:7673): the light-origin view for one GL cube face, +X,-X,+Y,
-Y,+Z,-Z. A rigid transform, so view-space length is world radial distance.
====================
*/
static void GLESD3_ShadowPointFaceView( const idVec3 &origin, int face, float matrix[16] ) {
	viewDef_t shadowView;
	memset( &shadowView, 0, sizeof( shadowView ) );
	shadowView.renderView.vieworg = origin;
	idMat3 &axis = shadowView.renderView.viewaxis;
	axis.Zero();
	switch ( face ) {
		case 0:	axis[0][0] = 1.0f;	axis[1][2] = 1.0f;	axis[2][1] = -1.0f;	break;
		case 1:	axis[0][0] = -1.0f;	axis[1][2] = -1.0f;	axis[2][1] = -1.0f;	break;
		case 2:	axis[0][1] = 1.0f;	axis[1][0] = -1.0f;	axis[2][2] = 1.0f;	break;
		case 3:	axis[0][1] = -1.0f;	axis[1][0] = -1.0f;	axis[2][2] = -1.0f;	break;
		case 4:	axis[0][2] = 1.0f;	axis[1][0] = -1.0f;	axis[2][1] = -1.0f;	break;
		default: axis[0][2] = -1.0f; axis[1][0] = 1.0f;	axis[2][1] = -1.0f;	break;
	}
	R_SetViewMatrix( &shadowView );
	memcpy( matrix, shadowView.worldSpace.modelViewMatrix, sizeof( float ) * 16 );
}

/*
====================
GLESD3_ShadowRenderLight

Renders the light's one or two maps. Returns false if a map could not be
rendered, in which case the caller sends the light back to stencil.
====================
*/
static bool GLESD3_ShadowRenderLight( glesShadowLight_t &light ) {
	const viewLight_t *vLight = light.vLight;
	const float casterOffset[4] = {
		r_shadowMapPolygonFactor.GetFloat(),
		r_shadowMapPolygonOffset.GetFloat() * ( 1.0f / 16777216.0f ),
		light.pointFar,
		1.0f
	};

	for ( int pass = 0; pass < 2; pass++ ) {
		const int slotIndex = light.slot[ pass ];
		if ( slotIndex < 0 ) {
			continue;
		}
		// GLOBAL aliases LOCAL when the light has no local casters
		if ( pass == GLESD3_SHADOW_PASS_GLOBAL && slotIndex == light.slot[ GLESD3_SHADOW_PASS_LOCAL ] ) {
			continue;
		}
		const glesShadowSlot_t &slot = light.point ? gles_shadowCube[ slotIndex ] : gles_shadow2D[ slotIndex ];

		if ( light.point ) {
			const float n = GLESD3_POINT_SHADOW_NEAR;
			const float f = light.pointFar;
			const float pointTerms[4] = { -( f + n ) / ( f - n ), -( 2.0f * f * n ) / ( f - n ), f, 0.0f };
			const idPlane unusedPlane( 0.0f, 0.0f, 0.0f, 0.0f );
			for ( int face = 0; face < 6; face++ ) {
				if ( !GLESD3_ShadowAttach( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, slot.texture ) ) {
					return false;
				}
				glClear( GL_DEPTH_BUFFER_BIT );
				float faceView[16];
				GLESD3_ShadowPointFaceView( vLight->globalLightOrigin, face, faceView );
				GLESD3_ShadowDrawPassCasters( vLight, pass, true, faceView, unusedPlane, pointTerms,
						casterOffset, face );
			}
		} else {
			if ( !GLESD3_ShadowAttach( GL_TEXTURE_2D, slot.texture ) ) {
				return false;
			}
			glClear( GL_DEPTH_BUFFER_BIT );
			float lightMatrix[16];
			R_ShadowMapClipPlanesToGLMatrix( light.projected.clipPlanes[0], lightMatrix );
			const float unusedTerms[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			GLESD3_ShadowDrawPassCasters( vLight, pass, false, lightMatrix, light.projected.clipPlanes[0][2],
					unusedTerms, casterOffset, 0 );
		}
	}
	return true;
}

/*
====================
RB_GLESD3_RenderShadowMaps

Selects this view's mapped lights and renders their maps. Called by the view
driver before the depth prepass; restores the framebuffer it found.
====================
*/
void RB_GLESD3_RenderShadowMaps( void ) {
	gles_numShadowLights = 0;
	gles_shadowViewDef = backEnd.viewDef;
	gles_activeShadowLight = NULL;
	gles_shadowMappedPasses = gles_shadowStencilPasses = gles_shadowCasterDraws = 0;

	const viewDef_t *viewDef = backEnd.viewDef;
	if ( !r_useShadowMap.GetBool() || !r_shadows.GetBool()
			|| viewDef == NULL || viewDef->viewEntitys == NULL || viewDef->viewLights == NULL ) {
		return;
	}
	// subviews (mirrors, remote cameras) keep stencil, the default
	// r_shadowMapSubviewPolicy behaviour on the other back ends
	if ( viewDef->isSubview ) {
		return;
	}
	const int maxLights = Min( r_glesD3ShadowMapMaxLights.GetInteger(), GLESD3_SHADOW_MAX_SLOTS );
	if ( maxLights <= 0 ) {
		return;
	}

	// candidates, largest on screen first
	const viewLight_t *candidates[ GLESD3_SHADOW_MAX_LIGHTS ];
	int numCandidates = 0;
	for ( const viewLight_t *vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		if ( numCandidates < GLESD3_SHADOW_MAX_LIGHTS && GLESD3_ShadowLightEligible( vLight ) ) {
			candidates[ numCandidates++ ] = vLight;
		}
	}
	for ( int i = 1; i < numCandidates; i++ ) {
		const viewLight_t *v = candidates[i];
		const int area = GLESD3_ShadowScissorArea( v );
		int j = i - 1;
		while ( j >= 0 && GLESD3_ShadowScissorArea( candidates[j] ) < area ) {
			candidates[j + 1] = candidates[j];
			j--;
		}
		candidates[j + 1] = v;
	}

	const int projectedSize = idMath::ClampInt( 128, 4096, r_shadowMapSize.GetInteger() );
	const int pointSize = idMath::ClampInt( 128, 2048, r_shadowMapPointSize.GetInteger() );
	int used2D = 0;
	int usedCube = 0;

	for ( int c = 0; c < numCandidates && gles_numShadowLights < GLESD3_SHADOW_MAX_LIGHTS; c++ ) {
		const viewLight_t *vLight = candidates[c];
		glesShadowLight_t &light = gles_shadowLights[ gles_numShadowLights ];
		memset( &light, 0, sizeof( light ) );
		light.vLight = vLight;
		light.point = vLight->pointLight && !vLight->parallel;
		light.size = light.point ? pointSize : projectedSize;
		light.slot[0] = light.slot[1] = -1;

		const bool passNeeded[2] = {
			vLight->localInteractions != NULL,
			vLight->globalInteractions != NULL
				|| ( r_shadowMapTranslucentReceivers.GetBool() && vLight->translucentInteractions != NULL )
		};
		int casterCount[2] = { 0, 0 };
		const bool representable[2] = {
			passNeeded[0] && GLESD3_ShadowPassRepresentable( vLight, GLESD3_SHADOW_PASS_LOCAL, casterCount[0] ),
			passNeeded[1] && GLESD3_ShadowPassRepresentable( vLight, GLESD3_SHADOW_PASS_GLOBAL, casterCount[1] )
		};
		if ( !representable[0] && !representable[1] ) {
			continue;
		}

		if ( light.point ) {
			light.pointFar = R_ShadowMapPointFarDistance( vLight );
			if ( !( light.pointFar > GLESD3_POINT_SHADOW_NEAR ) ) {
				continue;
			}
			const shadowMapPointReceiverSettings_t settings = R_ShadowMapPointReceiverSettings( light.pointFar, light.size );
			light.bias[0] = settings.constantBias;
			light.bias[1] = settings.normalBias;
			light.bias[2] = settings.texelBiasScale / (float)light.size;
			light.bias[3] = 2.0f * settings.normalOffsetScale / (float)light.size;
			light.filter[0] = Max( 0.0f, r_shadowMapPointFilterRadius.GetFloat() );
			light.filter[1] = (float)idMath::ClampInt( 1, 13, r_shadowMapPointFilterTaps.GetInteger() );
			light.filter[3] = 2.0f / (float)light.size;
		} else {
			R_BuildShadowMapProjectedLightState( vLight, viewDef, light.size, light.projected );
			if ( !light.projected.valid ) {
				continue;
			}
			const shadowMapProjectedFilterSettings_t filter = R_ShadowMapProjectedFilterSettings( vLight );
			light.bias[0] = r_shadowMapBias.GetFloat() * light.projected.biasScale[0];
			light.bias[1] = r_shadowMapNormalBias.GetFloat() * light.projected.biasScale[0];
			light.bias[2] = light.projected.texelDepthBias[0];
			light.bias[3] = light.projected.worldTexelSize[0] * r_shadowMapNormalOffsetScale.GetFloat();
			light.filter[0] = filter.filterRadius;
			light.filter[1] = (float)filter.filterTaps;
			light.filter[3] = 1.0f / (float)light.size;
		}

		// slots: one map per representable pass with casters, GLOBAL sharing
		// LOCAL's when the light has no local casters to add
		const bool hasLocalCasters = vLight->localShadowMapCasters != NULL
				|| vLight->localShadowMapDynamicCasters != NULL;
		int &used = light.point ? usedCube : used2D;
		bool ok = true;
		for ( int pass = 0; pass < 2 && ok; pass++ ) {
			if ( !representable[ pass ] ) {
				continue;
			}
			if ( casterCount[ pass ] == 0 ) {
				light.slot[ pass ] = GLESD3_SHADOW_SLOT_UNSHADOWED;
				continue;
			}
			if ( pass == GLESD3_SHADOW_PASS_GLOBAL && !hasLocalCasters && light.slot[0] >= 0 ) {
				light.slot[ pass ] = light.slot[0];
				continue;
			}
			if ( used >= maxLights ) {
				break;	// the pool is full: this pass stays stencil
			}
			glesShadowSlot_t &slot = light.point ? gles_shadowCube[ used ] : gles_shadow2D[ used ];
			if ( !GLESD3_ShadowEnsureSlot( slot, light.point, light.size ) ) {
				// this pass stays stencil, and no more maps are attempted this
				// view; the next view retries
				ok = false;
				break;
			}
			light.slot[ pass ] = used++;
		}
		if ( light.slot[0] != -1 || light.slot[1] != -1 ) {
			gles_numShadowLights++;
		}
		if ( !ok ) {
			break;
		}
	}

	if ( gles_numShadowLights == 0 ) {
		return;
	}

	// render every map, then hand the scene target back exactly as found
	GLint previousFbo = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFbo );
	const GLboolean scissorWasEnabled = glIsEnabled( GL_SCISSOR_TEST );

	R_GLESD3_Draw_BeginView();
	GL_State( GLS_DEPTHFUNC_LESS | GLS_COLORMASK | GLS_ALPHAMASK );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_POLYGON_OFFSET_FILL );
	glClearDepthf( 1.0f );
	gles_shadowCullApplied = -1;

	for ( int i = 0; i < gles_numShadowLights; i++ ) {
		glesShadowLight_t &light = gles_shadowLights[i];
		glViewport( 0, 0, light.size, light.size );
		if ( !GLESD3_ShadowRenderLight( light ) ) {
			light.slot[0] = light.slot[1] = -1;
		}
	}

	R_GLESD3_Draw_EndView();
	glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)previousFbo );
	if ( scissorWasEnabled ) {
		glEnable( GL_SCISSOR_TEST );
	}
	// leave culling in a state GL_Cull can track from
	glDisable( GL_CULL_FACE );
	backEnd.glState.faceCulling = CT_TWO_SIDED;
	backEnd.currentScissor.x1 = backEnd.currentScissor.y1 = -1;
	backEnd.currentScissor.x2 = backEnd.currentScissor.y2 = -2;
	backEnd.glState.forceGlState = true;
}

/*
===============================================================================
	Receivers
===============================================================================
*/

static const glesShadowLight_t *GLESD3_ShadowFindLight( const viewLight_t *vLight ) {
	if ( gles_shadowViewDef != backEnd.viewDef ) {
		return NULL;
	}
	for ( int i = 0; i < gles_numShadowLights; i++ ) {
		if ( gles_shadowLights[i].vLight == vLight ) {
			return &gles_shadowLights[i];
		}
	}
	return NULL;
}

/*
====================
RB_GLESD3_ShadowMapPassMode

How a light's LOCAL or GLOBAL receivers are shadowed this view.
====================
*/
glesD3ShadowPassMode_t RB_GLESD3_ShadowMapPassMode( const viewLight_t *vLight, bool globalPass ) {
	const glesShadowLight_t *light = GLESD3_ShadowFindLight( vLight );
	if ( light == NULL ) {
		return GLESD3_SHADOWPASS_STENCIL;
	}
	const int slot = light->slot[ globalPass ? GLESD3_SHADOW_PASS_GLOBAL : GLESD3_SHADOW_PASS_LOCAL ];
	if ( slot == GLESD3_SHADOW_SLOT_UNSHADOWED ) {
		return GLESD3_SHADOWPASS_UNSHADOWED;
	}
	return slot >= 0 ? GLESD3_SHADOWPASS_MAP : GLESD3_SHADOWPASS_STENCIL;
}

/*
====================
RB_GLESD3_ShadowMapBeginPass

Binds the pass's map on unit 6 and returns the receiver program, or NULL when
the pass is not mapped.
====================
*/
glesProgram_t *RB_GLESD3_ShadowMapBeginPass( const viewLight_t *vLight, bool globalPass ) {
	gles_activeShadowLight = NULL;
	const glesShadowLight_t *light = GLESD3_ShadowFindLight( vLight );
	if ( light == NULL ) {
		return NULL;
	}
	const int slot = light->slot[ globalPass ? GLESD3_SHADOW_PASS_GLOBAL : GLESD3_SHADOW_PASS_LOCAL ];
	if ( slot < 0 ) {
		return NULL;
	}
	glesProgram_t *program = R_GLESD3_Program( light->point
			? GLESD3_PROGRAM_INTERACTION_SHADOW_POINT : GLESD3_PROGRAM_INTERACTION_SHADOW );
	if ( program == NULL ) {
		return NULL;
	}
	GL_SelectTexture( 6 );
	R_BindTextureForDirectAccess( light->point ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D,
			light->point ? gles_shadowCube[ slot ].texture : gles_shadow2D[ slot ].texture );
	GL_SelectTexture( 0 );
	gles_activeShadowLight = light;
	gles_shadowMappedPasses++;
	return program;
}

void RB_GLESD3_ShadowMapEndPass( void ) {
	if ( gles_activeShadowLight == NULL ) {
		return;
	}
	GL_SelectTexture( 6 );
	R_BindTextureForDirectAccess( gles_activeShadowLight->point ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, 0 );
	GL_SelectTexture( 0 );
	gles_activeShadowLight = NULL;
}

void RB_GLESD3_ShadowMapCountStencilPass( void ) {
	gles_shadowStencilPasses++;
}

/*
====================
RB_GLESD3_ShadowMapSetReceiverUniforms

Per-surface receiver state: the light clip planes localized to the surface's
model space (projected), or its model rows (point), plus the light's bias and
filter parameters.
====================
*/
void RB_GLESD3_ShadowMapSetReceiverUniforms( const glesProgram_t *program, const drawSurf_t *surf ) {
	const glesShadowLight_t *light = gles_activeShadowLight;
	if ( light == NULL || program == NULL ) {
		return;
	}
	const float *m = surf->space->modelMatrix;
	if ( light->point ) {
		const float row0[4] = { m[0], m[4], m[8],  m[12] };
		const float row1[4] = { m[1], m[5], m[9],  m[13] };
		const float row2[4] = { m[2], m[6], m[10], m[14] };
		glUniform4fv( program->uModelRow0, 1, row0 );
		glUniform4fv( program->uModelRow1, 1, row1 );
		glUniform4fv( program->uModelRow2, 1, row2 );
		const idVec3 &origin = light->vLight->globalLightOrigin;
		glUniform4f( program->uShadowLightOriginFar, origin[0], origin[1], origin[2], light->pointFar );
	} else {
		idPlane local[4];
		for ( int i = 0; i < 4; i++ ) {
			R_GlobalPlaneToLocal( m, light->projected.clipPlanes[0][i], local[i] );
		}
		glUniform4fv( program->uShadowRow0, 1, local[0].ToFloatPtr() );
		glUniform4fv( program->uShadowRow1, 1, local[1].ToFloatPtr() );
		glUniform4fv( program->uShadowRow2, 1, local[2].ToFloatPtr() );
		glUniform4fv( program->uShadowRow3, 1, local[3].ToFloatPtr() );
	}
	glUniform4fv( program->uShadowBias, 1, light->bias );
	glUniform4fv( program->uShadowFilter, 1, light->filter );
}

void RB_GLESD3_ShadowMapReport( int &mappedLights, int &pointLights, int &mappedPasses, int &stencilPasses, int &casterDraws ) {
	mappedLights = ( gles_shadowViewDef == backEnd.viewDef ) ? gles_numShadowLights : 0;
	pointLights = 0;
	for ( int i = 0; i < mappedLights; i++ ) {
		pointLights += gles_shadowLights[i].point ? 1 : 0;
	}
	mappedPasses = gles_shadowMappedPasses;
	stencilPasses = gles_shadowStencilPasses;
	casterDraws = gles_shadowCasterDraws;
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
