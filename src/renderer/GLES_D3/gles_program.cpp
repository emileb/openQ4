// Copyright (C) 2026 DarkMatter Productions
//
/*
===============================================================================

	gles_d3 -- GLSL ES 300 compile, link and uniform resolution.


===============================================================================
*/

#ifdef OPENQ4_RENDERER_GLES_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "gles_d3_local.h"
#include "gles_program.h"

/*
====================
r_glesD3ShaderPath

Reload shaders from disk instead of the embedded strings: point it at a
directory holding <program>.vert / <program>.frag and run
`reloadGLESD3Shaders`. Empty (the default) uses the embedded sources, which
is what an Android build ships with.

A file that is missing or unreadable falls back to the embedded source for
that stage and says so, so a typo in the path degrades to "no override"
rather than to a backend with no programs.
====================
*/
static idCVar r_glesD3ShaderPath( "r_glesD3ShaderPath", "", CVAR_RENDERER,
		"gles_d3: directory to load <program>.vert/.frag from; empty uses the embedded sources" );

static glesProgram_t	gles_programs[ GLESD3_PROGRAM_COUNT ];
static GLuint			gles_currentProgram = 0;

/*
====================
GLESD3_PrintInfoLog
====================
*/
static void GLESD3_PrintInfoLog( GLuint object, bool isProgram, const char *label ) {
	GLint length = 0;
	if ( isProgram ) {
		glGetProgramiv( object, GL_INFO_LOG_LENGTH, &length );
	} else {
		glGetShaderiv( object, GL_INFO_LOG_LENGTH, &length );
	}
	if ( length <= 1 ) {
		return;
	}

	char *log = (char *)Mem_Alloc( length + 1 );
	if ( log == NULL ) {
		return;
	}
	if ( isProgram ) {
		glGetProgramInfoLog( object, length, NULL, log );
	} else {
		glGetShaderInfoLog( object, length, NULL, log );
	}
	log[ length ] = '\0';
	common->Printf( "gles_d3 %s:\n%s\n", label, log );
	Mem_Free( log );
}

/*
====================
GLESD3_LoadOverrideSource

Returns NULL when no override is configured or the file is unusable; the
caller then keeps the embedded source.
====================
*/
static char *GLESD3_LoadOverrideSource( const char *programName, GLenum stage ) {
	const char *directory = r_glesD3ShaderPath.GetString();
	if ( directory == NULL || directory[0] == '\0' ) {
		return NULL;
	}

	idStr path = directory;
	path.AppendPath( programName );
	path.Append( stage == GL_VERTEX_SHADER ? ".vert" : ".frag" );

	char *buffer = NULL;
	const int length = fileSystem->ReadFile( path.c_str(), (void **)&buffer );
	if ( length <= 0 || buffer == NULL ) {
		common->Warning( "gles_d3: shader override '%s' unreadable; using the embedded source", path.c_str() );
		return NULL;
	}
	common->Printf( "gles_d3: shader override %s\n", path.c_str() );
	return buffer;
}

/*
====================
GLESD3_CompileStage
====================
*/
static GLuint GLESD3_CompileStage( glesD3ProgramId_t id, GLenum stage, const char *programName ) {
	const char *embedded = R_GLESD3_EmbeddedShaderSource( id, stage );
	if ( embedded == NULL ) {
		return 0;
	}

	char *override = GLESD3_LoadOverrideSource( programName, stage );
	const char *body = ( override != NULL ) ? override : embedded;

	const GLuint shader = glCreateShader( stage );
	if ( shader == 0 ) {
		if ( override != NULL ) {
			fileSystem->FreeFile( override );
		}
		return 0;
	}

	// Each source under GLES_D3/glsl/ is a complete program -- `#version 300
	// es` and the precision defaults included -- so nothing is prepended here.
	// That keeps a shader file pasteable into a validator unchanged, and makes
	// a disk override byte-identical to the embedded source.
	glShaderSource( shader, 1, &body, NULL );
	glCompileShader( shader );

	GLint compiled = GL_FALSE;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
	if ( compiled != GL_TRUE ) {
		common->Warning( "gles_d3: %s %s shader failed to compile",
				programName, stage == GL_VERTEX_SHADER ? "vertex" : "fragment" );
		GLESD3_PrintInfoLog( shader, false, "shader log" );
		glDeleteShader( shader );
		if ( override != NULL ) {
			fileSystem->FreeFile( override );
		}
		return 0;
	}

	if ( override != NULL ) {
		fileSystem->FreeFile( override );
	}
	return shader;
}

/*
====================
GLESD3_ResolveUniforms
====================
*/
static void GLESD3_ResolveUniforms( glesProgram_t *program ) {
	program->uMVP = glGetUniformLocation( program->program, "uMVP" );
	program->uColor = glGetUniformLocation( program->program, "uColor" );
	program->uTextureMatrix = glGetUniformLocation( program->program, "uTextureMatrix" );
	program->uAlphaTest = glGetUniformLocation( program->program, "uAlphaTest" );
	program->uTexture0 = glGetUniformLocation( program->program, "uTexture0" );
	program->uTexture1 = glGetUniformLocation( program->program, "uTexture1" );
	program->uTexture2 = glGetUniformLocation( program->program, "uTexture2" );
	// GL accepts either spelling for element 0 of an array, but drivers differ
	// on which they return from the query, so ask for both.
	program->uParms = glGetUniformLocation( program->program, "uParms" );
	if ( program->uParms < 0 ) {
		program->uParms = glGetUniformLocation( program->program, "uParms[0]" );
	}
	program->uTexMatrixS = glGetUniformLocation( program->program, "uTexMatrixS" );
	program->uTexMatrixT = glGetUniformLocation( program->program, "uTexMatrixT" );
	program->uVertexColor = glGetUniformLocation( program->program, "uVertexColor" );

	program->uLocalLightOrigin = glGetUniformLocation( program->program, "uLocalLightOrigin" );
	program->uLocalViewOrigin = glGetUniformLocation( program->program, "uLocalViewOrigin" );
	program->uLightProjectionS = glGetUniformLocation( program->program, "uLightProjectionS" );
	program->uLightProjectionT = glGetUniformLocation( program->program, "uLightProjectionT" );
	program->uLightProjectionQ = glGetUniformLocation( program->program, "uLightProjectionQ" );
	program->uLightFalloffS = glGetUniformLocation( program->program, "uLightFalloffS" );
	program->uBumpMatrixS = glGetUniformLocation( program->program, "uBumpMatrixS" );
	program->uBumpMatrixT = glGetUniformLocation( program->program, "uBumpMatrixT" );
	program->uDiffuseMatrixS = glGetUniformLocation( program->program, "uDiffuseMatrixS" );
	program->uDiffuseMatrixT = glGetUniformLocation( program->program, "uDiffuseMatrixT" );
	program->uSpecularMatrixS = glGetUniformLocation( program->program, "uSpecularMatrixS" );
	program->uSpecularMatrixT = glGetUniformLocation( program->program, "uSpecularMatrixT" );
	program->uDiffuseColor = glGetUniformLocation( program->program, "uDiffuseColor" );
	program->uSpecularColor = glGetUniformLocation( program->program, "uSpecularColor" );
	program->uAmbientLight = glGetUniformLocation( program->program, "uAmbientLight" );
	program->uAmbientDir = glGetUniformLocation( program->program, "uAmbientDir" );
	program->uLightOrigin = glGetUniformLocation( program->program, "uLightOrigin" );

	program->uFogDistanceS = glGetUniformLocation( program->program, "uFogDistanceS" );
	program->uFogEnterS = glGetUniformLocation( program->program, "uFogEnterS" );
	program->uFogEnterT = glGetUniformLocation( program->program, "uFogEnterT" );

	program->uCubeMap = glGetUniformLocation( program->program, "uCubeMap" );
	program->uModelRow0 = glGetUniformLocation( program->program, "uModelRow0" );
	program->uModelRow1 = glGetUniformLocation( program->program, "uModelRow1" );
	program->uModelRow2 = glGetUniformLocation( program->program, "uModelRow2" );

	// The interaction program's samplers are fixed units matching the Vulkan
	// descriptor sets; bound once at link like uTexture0/1 above.
	static const struct { const char *name; int unit; } interactionSamplers[] = {
		{ "uSpecularTableMap", 0 }, { "uBumpMap", 1 }, { "uLightFalloffMap", 2 },
		{ "uLightProjectionMap", 3 }, { "uDiffuseMap", 4 }, { "uSpecularMap", 5 },
	};

	// sampler bindings are link-time constants for this backend: unit N is
	// always uTextureN, so no pass has to re-issue them per draw
	glUseProgram( program->program );
	if ( program->uTexture0 >= 0 ) {
		glUniform1i( program->uTexture0, 0 );
	}
	if ( program->uTexture1 >= 0 ) {
		glUniform1i( program->uTexture1, 1 );
	}
	if ( program->uTexture2 >= 0 ) {
		glUniform1i( program->uTexture2, 2 );
	}
	// the cube sampler shares unit 0 with uTexture0; no program declares both
	if ( program->uCubeMap >= 0 ) {
		glUniform1i( program->uCubeMap, 0 );
	}
	for ( size_t i = 0; i < sizeof( interactionSamplers ) / sizeof( interactionSamplers[0] ); i++ ) {
		const GLint location = glGetUniformLocation( program->program, interactionSamplers[i].name );
		if ( location >= 0 ) {
			glUniform1i( location, interactionSamplers[i].unit );
		}
	}
	glUseProgram( 0 );
	gles_currentProgram = 0;
}

/*
====================
GLESD3_BuildProgram
====================
*/
static bool GLESD3_BuildProgram( glesD3ProgramId_t id ) {
	glesProgram_t *program = &gles_programs[ id ];
	const char *programName = R_GLESD3_ProgramName( id );

	memset( program, 0, sizeof( *program ) );
	idStr::Copynz( program->name, programName, sizeof( program->name ) );
	program->uMVP = program->uColor = program->uTextureMatrix = -1;
	program->uAlphaTest = program->uTexture0 = program->uTexture1 = -1;
	program->uTexMatrixS = program->uTexMatrixT = program->uVertexColor = -1;
	program->uLocalLightOrigin = program->uLocalViewOrigin = -1;
	program->uLightProjectionS = program->uLightProjectionT = program->uLightProjectionQ = -1;
	program->uLightFalloffS = program->uBumpMatrixS = program->uBumpMatrixT = -1;
	program->uDiffuseMatrixS = program->uDiffuseMatrixT = -1;
	program->uSpecularMatrixS = program->uSpecularMatrixT = -1;
	program->uDiffuseColor = program->uSpecularColor = -1;
	program->uAmbientLight = program->uAmbientDir = program->uLightOrigin = -1;
	program->uFogDistanceS = program->uFogEnterS = program->uFogEnterT = -1;
	program->uCubeMap = -1;
	program->uModelRow0 = program->uModelRow1 = program->uModelRow2 = -1;

	const GLuint vertexShader = GLESD3_CompileStage( id, GL_VERTEX_SHADER, programName );
	const GLuint fragmentShader = GLESD3_CompileStage( id, GL_FRAGMENT_SHADER, programName );
	if ( vertexShader == 0 || fragmentShader == 0 ) {
		if ( vertexShader != 0 ) {
			glDeleteShader( vertexShader );
		}
		if ( fragmentShader != 0 ) {
			glDeleteShader( fragmentShader );
		}
		return false;
	}

	const GLuint handle = glCreateProgram();
	if ( handle == 0 ) {
		glDeleteShader( vertexShader );
		glDeleteShader( fragmentShader );
		return false;
	}
	glAttachShader( handle, vertexShader );
	glAttachShader( handle, fragmentShader );

	// The shader bodies already carry layout(location=), which ES 300 honours;
	// binding here as well keeps one authority for the numbering and covers a
	// disk override that forgot the qualifiers.
	glBindAttribLocation( handle, GLESD3_ATTR_POSITION, "inPosition" );
	glBindAttribLocation( handle, GLESD3_ATTR_COLOR, "inColor" );
	glBindAttribLocation( handle, GLESD3_ATTR_NORMAL, "inNormal" );
	glBindAttribLocation( handle, GLESD3_ATTR_TANGENT, "inTangent" );
	glBindAttribLocation( handle, GLESD3_ATTR_BITANGENT, "inBitangent" );
	glBindAttribLocation( handle, GLESD3_ATTR_TEXCOORD, "inTexCoord" );
	glBindAttribLocation( handle, GLESD3_ATTR_TEXDIR, "inTexDir" );

	glLinkProgram( handle );

	// the shaders are reference-counted by the program once attached
	glDeleteShader( vertexShader );
	glDeleteShader( fragmentShader );

	GLint linked = GL_FALSE;
	glGetProgramiv( handle, GL_LINK_STATUS, &linked );
	if ( linked != GL_TRUE ) {
		common->Warning( "gles_d3: program '%s' failed to link", programName );
		GLESD3_PrintInfoLog( handle, true, "program log" );
		glDeleteProgram( handle );
		return false;
	}

	program->program = handle;
	GLESD3_ResolveUniforms( program );
	return true;
}

/*
====================
R_GLESD3_Programs_Init
====================
*/
bool R_GLESD3_Programs_Init( void ) {
	R_GLESD3_Programs_Shutdown();

	int built = 0;
	int failed = 0;
	for ( int i = 0; i < GLESD3_PROGRAM_COUNT; i++ ) {
		if ( GLESD3_BuildProgram( (glesD3ProgramId_t)i ) ) {
			built++;
		} else {
			failed++;
		}
	}

	common->Printf( "gles_d3 shader library: programs=%i/%i failed=%i\n",
			built, (int)GLESD3_PROGRAM_COUNT, failed );
	return failed == 0;
}

/*
====================
R_GLESD3_Programs_Shutdown
====================
*/
void R_GLESD3_Programs_Shutdown( void ) {
	for ( int i = 0; i < GLESD3_PROGRAM_COUNT; i++ ) {
		if ( gles_programs[ i ].program != 0 ) {
			glDeleteProgram( gles_programs[ i ].program );
		}
		memset( &gles_programs[ i ], 0, sizeof( gles_programs[ i ] ) );
	}
	if ( gles_currentProgram != 0 ) {
		glUseProgram( 0 );
		gles_currentProgram = 0;
	}
}

/*
====================
R_GLESD3_Program
====================
*/
glesProgram_t *R_GLESD3_Program( glesD3ProgramId_t id ) {
	if ( id < 0 || id >= GLESD3_PROGRAM_COUNT ) {
		return NULL;
	}
	if ( gles_programs[ id ].program == 0 ) {
		return NULL;
	}
	return &gles_programs[ id ];
}

/*
====================
R_GLESD3_UseProgram
====================
*/
void R_GLESD3_UseProgram( const glesProgram_t *program ) {
	const GLuint handle = ( program != NULL ) ? program->program : 0;
	if ( handle == gles_currentProgram ) {
		return;
	}
	glUseProgram( handle );
	gles_currentProgram = handle;
}

/*
====================
R_GLESD3_InvalidateProgramState
====================
*/
void R_GLESD3_InvalidateProgramState( void ) {
	gles_currentProgram = 0;
}

/*
====================
R_GLESD3_ReloadShaders_f
====================
*/
static void R_GLESD3_ReloadShaders_f( const idCmdArgs &args ) {
	( void )args;
	if ( !RB_GLESD3_Active() ) {
		common->Printf( "reloadGLESD3Shaders: gles_d3 is not the active back end\n" );
		return;
	}
	R_GLESD3_Programs_Init();
}

/*
====================
R_GLESD3_Programs_RegisterCommands
====================
*/
void R_GLESD3_Programs_RegisterCommands( void ) {
	cmdSystem->AddCommand( "reloadGLESD3Shaders", R_GLESD3_ReloadShaders_f, CMD_FL_RENDERER,
			"gles_d3: rebuild the shader programs, picking up r_glesD3ShaderPath" );
}

#endif /* OPENQ4_RENDERER_GLES_MODULE */
