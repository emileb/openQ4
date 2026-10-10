// Copyright (C) 2026 DarkMatter Productions
//
// Per-light bump/diffuse/specular -- vertex stage.
//
// A direct port of src/renderer/Vulkan/shaders/interaction.vert, which already
// carries the stock Quake 4 interaction.vfp parity math. Attribute locations
// match that file exactly (position 0, colour 1, normal 2, tangent 3,
// bitangent 4, texcoord 5); the two shader sets stay diffable only while they
// agree, so change both or neither.

#include "glsl_shaders.h"

const char * const glesInteractionShaderVP = R"(#version 300 es
precision highp float;
precision highp int;

// Bit-identical gl_Position across programs. See glsl_shaders.h -- without this
// the depth-EQUAL passes reject geometry the depth prepass wrote.
invariant gl_Position;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec2 inTexCoord;

uniform mat4 uMVP;

uniform vec4 uLocalLightOrigin;
uniform vec4 uLocalViewOrigin;

uniform vec4 uLightProjectionS;
uniform vec4 uLightProjectionT;
uniform vec4 uLightProjectionQ;
uniform vec4 uLightFalloffS;

uniform vec4 uBumpMatrixS;
uniform vec4 uBumpMatrixT;
uniform vec4 uDiffuseMatrixS;
uniform vec4 uDiffuseMatrixT;
uniform vec4 uSpecularMatrixS;
uniform vec4 uSpecularMatrixT;

// (rgbMul, rgbAdd, alphaMul, alphaAdd) -- the SVC packing, shared with the
// material shader. Lighting has no alpha term, so only .xy is read here; the
// layout is kept identical so the two paths stay diffable.
uniform vec4 uVertexColor;

out vec2 vBumpTexCoord;
out vec2 vDiffuseTexCoord;
out vec2 vSpecularTexCoord;
out vec4 vLightFalloffTexCoord;
out vec4 vLightProjectionTexCoord;
out vec3 vLightVector;
out vec3 vHalfAngleVector;
out vec3 vVertexColor;
out vec3 vViewVector;

// Shadow-map receivers (GLESD3_PROGRAM_INTERACTION_SHADOW*), from
// Vulkan/shaders/interaction_shadow.vert and interaction_shadow_point.vert.
// Normal-offset shadows push the sampled point along the geometric normal by
// (texel size x sin theta) before projecting, which fixes self-shadow acne
// without the contact-shadow detachment a larger depth bias would cause.
// uShadowBias.w is that normal offset in world units (projected) or per unit
// of distance (point).
#ifdef GLESD3_SHADOW_PROJECTED
uniform vec4 uShadowRow0;
uniform vec4 uShadowRow1;
uniform vec4 uShadowRow2;
uniform vec4 uShadowRow3;
uniform vec4 uShadowBias;
out vec4 vShadowCoord;
out float vShadowLightCos;
#endif
#ifdef GLESD3_SHADOW_POINT
uniform vec4 uModelRow0;
uniform vec4 uModelRow1;
uniform vec4 uModelRow2;
uniform vec4 uShadowLightOriginFar;
uniform vec4 uShadowBias;
out vec3 vPointShadowVector;
out float vShadowLightCos;
#endif

vec3 TangentSpaceVector(vec3 objectVector) {
    return vec3(
        dot(inTangent, objectVector),
        dot(inBitangent, objectVector),
        dot(inNormal, objectVector));
}

void main() {
    vec4 position = vec4(inPosition, 1.0);
    vec4 texCoord = vec4(inTexCoord, 0.0, 1.0);

    vec3 toLight = uLocalLightOrigin.xyz - position.xyz;
    vec3 toView = uLocalViewOrigin.xyz - position.xyz;

    vLightVector = TangentSpaceVector(toLight);
    vHalfAngleVector = TangentSpaceVector(normalize(toLight) + normalize(toView));
    vViewVector = TangentSpaceVector(toView);

    vBumpTexCoord = vec2(dot(texCoord, uBumpMatrixS), dot(texCoord, uBumpMatrixT));
    vDiffuseTexCoord = vec2(dot(texCoord, uDiffuseMatrixS), dot(texCoord, uDiffuseMatrixT));
    vSpecularTexCoord = vec2(dot(texCoord, uSpecularMatrixS), dot(texCoord, uSpecularMatrixT));

    // z is unused by textureProj for this 2D sampler; carry model-local Z
    vLightFalloffTexCoord = vec4(dot(position, uLightFalloffS), 0.5, position.z, 1.0);

    vLightProjectionTexCoord = vec4(
        dot(position, uLightProjectionS),
        dot(position, uLightProjectionT),
        0.0,
        dot(position, uLightProjectionQ));

    vVertexColor = inColor.rgb * uVertexColor.x + vec3(uVertexColor.y);

#if defined(GLESD3_SHADOW_PROJECTED) || defined(GLESD3_SHADOW_POINT)
    vec3 shadowNormal = normalize(inNormal);
    float shadowLightCos = max(dot(shadowNormal, normalize(toLight)), 0.0);
    float shadowSinTheta = sqrt(max(1.0 - shadowLightCos * shadowLightCos, 0.0));
    vShadowLightCos = shadowLightCos;
#endif
#ifdef GLESD3_SHADOW_PROJECTED
    vec4 offsetPosition = vec4(position.xyz + shadowNormal * (uShadowBias.w * shadowSinTheta), 1.0);
    vShadowCoord = vec4(
        dot(offsetPosition, uShadowRow0),
        dot(offsetPosition, uShadowRow1),
        dot(offsetPosition, uShadowRow2),
        dot(offsetPosition, uShadowRow3));
#endif
#ifdef GLESD3_SHADOW_POINT
    vec3 worldPos = vec3(dot(position, uModelRow0), dot(position, uModelRow1), dot(position, uModelRow2));
    vec3 pointShadowVector = worldPos - uShadowLightOriginFar.xyz;
    vec3 worldNormal = normalize(vec3(
        dot(vec4(inNormal, 0.0), uModelRow0),
        dot(vec4(inNormal, 0.0), uModelRow1),
        dot(vec4(inNormal, 0.0), uModelRow2)));
    float normalOffset = uShadowBias.w * length(pointShadowVector) * shadowSinTheta;
    vPointShadowVector = pointShadowVector + worldNormal * normalOffset;
#endif

    gl_Position = uMVP * position;
}
)";
