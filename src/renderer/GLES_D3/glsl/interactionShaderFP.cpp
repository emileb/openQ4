// Copyright (C) 2026 DarkMatter Productions
//
// Per-light bump/diffuse/specular -- fragment stage.
//
// A direct port of src/renderer/Vulkan/shaders/interaction.frag. Everything
// Quake 4 specific comes across unchanged; listed here so a later divergence
// shows up as a diff rather than as a defect:
//
//   - bump decode: alpha=x, green=y, z rebuilt, no renormalization
//   - specular through the REAL _specularTable ramp, x2 because the CPU-side
//     ARB2 path doubles the specular env constant
//   - projected light falloff and light projection sampled with textureProj
//   - direction vectors normalized in-shader; no normalization cube map
//   - additive ONE:ONE, alpha written as 0 like the GL reference
//
// Ambient lights are why nothing in Quake 4 is ever fully black. They are
// ordinary lights flagged ambientLight, and every path substitutes a constant
// tangent-space direction for the per-pixel light vector: ARB2 by binding
// ambientNormalMap instead of the normalization cube map
// (draw_arb2.cpp:11460), this shader by its GLESD3_AMBIENT variant, which
// substitutes uAmbientDir at compile time. The cube's 8-bit quantization is
// applied CPU-side so the two agree exactly. Compiled standalone (no define)
// this file is the ordinary per-light case.
//
// Sampler units are 1:1 with the Vulkan descriptor sets:
//   0 specularTable   1 bump      2 lightFalloff
//   3 lightProjection 4 diffuse   5 specular

#include "glsl_shaders.h"

const char * const glesInteractionShaderFP = R"(#version 300 es
precision highp float;
precision highp int;

uniform sampler2D uSpecularTableMap;
uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;

#ifdef GLESD3_AMBIENT
uniform vec3 uAmbientDir;
#endif

in vec2 vBumpTexCoord;
in vec2 vDiffuseTexCoord;
in vec2 vSpecularTexCoord;
in vec4 vLightFalloffTexCoord;
in vec4 vLightProjectionTexCoord;
in vec3 vLightVector;
in vec3 vHalfAngleVector;
in vec3 vVertexColor;
in vec3 vViewVector;

out vec4 outColor;

// Shadow-map receivers, from Vulkan/shaders/interaction_shadow.frag and
// interaction_shadow_point.frag, reduced to one cascade and fixed PCF:
//   uShadowBias    x constant, y normal (sin theta) bias, z texel depth bias,
//                  w normal offset (consumed by the vertex stage)
//   uShadowFilter  x PCF radius in texels, y taps (1/5/9/13), w texel step
//                  (1 / map size projected, 2 / face size point)
// Samplers need an explicit precision in GLSL ES: shadow samplers have no
// default, and a mediump compare is too coarse for depth.
#if defined(GLESD3_SHADOW_PROJECTED) || defined(GLESD3_SHADOW_POINT)
uniform vec4 uShadowBias;
uniform vec4 uShadowFilter;
in float vShadowLightCos;

const vec2 kShadowPoisson[12] = vec2[12](
    vec2(-0.326212, -0.405805), vec2(-0.840144, -0.073580),
    vec2(-0.695914, 0.457137), vec2(-0.203345, 0.620716),
    vec2(0.962340, -0.194983), vec2(0.473434, -0.480026),
    vec2(0.519456, 0.767022), vec2(0.185461, -0.893124),
    vec2(0.507431, 0.064425), vec2(0.896420, 0.412458),
    vec2(-0.321940, -0.932615), vec2(-0.791559, -0.597705));

float ShadowReceiverBias() {
    float lightCos = clamp(vShadowLightCos, 0.20, 1.0);
    float sinTheta = sqrt(max(1.0 - lightCos * lightCos, 0.0));
    float slopeBias = min(sinTheta / lightCos, 4.0);
    float scalarBias = uShadowBias.x + uShadowBias.y * sinTheta;
    float texelBias = uShadowBias.z * (1.0 + slopeBias);
    return max(max(scalarBias, 0.0), max(texelBias, 0.0));
}

int ShadowExtraTaps() {
    // 1, 5, 9 or 13 taps: the centre plus 0, 4, 8 or 12 Poisson offsets
    return uShadowFilter.y <= 1.0 ? 0 : (uShadowFilter.y <= 5.0 ? 4 : (uShadowFilter.y <= 9.0 ? 8 : 12));
}
#endif

#ifdef GLESD3_SHADOW_PROJECTED
uniform highp sampler2DShadow uShadowMap;
in vec4 vShadowCoord;

float SampleShadowFactor() {
    float w = vShadowCoord.w;
    // !(w > eps) also rejects NaN; behind the projector is lit
    if (!(w > 1.0e-5) || w > 65536.0) {
        return 1.0;
    }
    vec2 uv = vShadowCoord.xy / w * 0.5 + 0.5;
    float depth = vShadowCoord.z;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0
            || depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }
    float compareDepth = depth - ShadowReceiverBias();
    vec2 guard = vec2(uShadowFilter.w * max(0.5, uShadowFilter.x + 0.75));
    vec2 clampMin = guard;
    vec2 clampMax = vec2(1.0) - guard;
    uv = clamp(uv, clampMin, clampMax);

    float result = texture(uShadowMap, vec3(uv, compareDepth));
    int extra = ShadowExtraTaps();
    if (uShadowFilter.x <= 0.0 || extra == 0) {
        return result;
    }
    vec2 tap = vec2(uShadowFilter.w * uShadowFilter.x);
    for (int i = 0; i < extra; i++) {
        result += texture(uShadowMap, vec3(clamp(uv + kShadowPoisson[i] * tap, clampMin, clampMax), compareDepth));
    }
    return result / float(extra + 1);
}
#endif

#ifdef GLESD3_SHADOW_POINT
uniform highp samplerCubeShadow uShadowMap;
uniform vec4 uShadowLightOriginFar;
in vec3 vPointShadowVector;

float SampleShadowFactor() {
    float far = uShadowLightOriginFar.w;
    if (!(far > 0.0)) {
        return 1.0;
    }
    float depth = length(vPointShadowVector) / far;
    if (depth <= 0.0 || depth >= 1.0) {
        return 1.0;
    }
    float compareDepth = depth - ShadowReceiverBias();
    vec3 direction = vPointShadowVector * inversesqrt(max(dot(vPointShadowVector, vPointShadowVector), 1.0e-8));

    float result = texture(uShadowMap, vec4(direction, compareDepth));
    int extra = ShadowExtraTaps();
    if (uShadowFilter.x <= 0.0 || extra == 0) {
        return result;
    }
    // offsets in the plane perpendicular to the lookup, scaled to cube texels
    vec3 up = abs(direction.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, direction));
    vec3 bitangent = cross(direction, tangent);
    float tap = uShadowFilter.w * uShadowFilter.x;
    for (int i = 0; i < extra; i++) {
        vec2 o = kShadowPoisson[i];
        result += texture(uShadowMap, vec4(normalize(direction + (tangent * o.x + bitangent * o.y) * tap), compareDepth));
    }
    return result / float(extra + 1);
}
#endif

vec3 SafeNormalize(vec3 value) {
    return value * inversesqrt(max(dot(value, value), 1.0e-8));
}

void main() {
    vec4 bumpSample = texture(uBumpMap, vBumpTexCoord);

    // X from alpha, Y from green. RXGB/DXT5nm puts X in alpha natively, and
    // gl_Image.cpp swizzles alpha <- red for every other bump format, so this
    // one pair of reads covers all of them.
    //
    // Z comes from blue where blue exists, and is rebuilt where it does not.
    // EAC_RG11 is a two-channel format, so its blue samples as exactly 0.0 and
    // decodes to -1.0; a real tangent-space normal has Z > 0 and can never
    // land there. Testing the decoded value therefore separates the two
    // without a shader permutation, and leaves every format that does store Z
    // reading the exact bits it always did -- reconstructing unconditionally
    // measured 1.24 RMSE against the DXT path, small but not nothing.
    //
    // A normal quantised to exactly Z = 0 may decode a hair below zero and
    // take the rebuild branch; sqrt() returns ~0 there, which is the same
    // answer. Still no renormalization on either side.
    vec2 localNormalXY = vec2(bumpSample.a, bumpSample.g) * 2.0 - 1.0;
    float storedZ = bumpSample.b * 2.0 - 1.0;
    float rebuiltZ = sqrt(max(1.0 - dot(localNormalXY, localNormalXY), 0.0));
    vec3 localNormal = vec3(localNormalXY, storedZ < 0.0 ? rebuiltZ : storedZ);

    // An ambient light lights from a constant direction instead of from the
    // light origin, which is what keeps unlit corners off pure black. The two
    // cases are separate compiled variants (gles_program.cpp, D8) rather than
    // a uniform test, so the common per-light case carries no branch at all.
#ifdef GLESD3_AMBIENT
    vec3 lightDir = uAmbientDir;
#else
    vec3 lightDir = SafeNormalize(vLightVector);
#endif
    float ndotl = max(dot(lightDir, localNormal), 0.0);

    vec3 light = vec3(ndotl);
    light *= textureProj(uLightFalloffMap, vLightFalloffTexCoord).rgb;
    light *= textureProj(uLightProjectionMap, vLightProjectionTexCoord).rgb;
#if defined(GLESD3_SHADOW_PROJECTED) || defined(GLESD3_SHADOW_POINT)
    // scales diffuse and specular alike, as in Vulkan and ARB2
    light *= SampleShadowFactor();
#endif

    vec3 diffuse = texture(uDiffuseMap, vDiffuseTexCoord).rgb * uDiffuseColor.rgb;

#ifdef GLESD3_AMBIENT
    // an ambient light has no specular term: GLESD3_SubmitInteraction binds
    // blackImage as the specular map, so the retail contribution is exactly
    // zero -- compiled out here rather than sampled and multiplied away
    vec3 specular = vec3(0.0);
#else
    vec3 halfAngle = SafeNormalize(vHalfAngleVector);
    float specularDot = clamp(dot(halfAngle, localNormal), 0.0, 1.0);
    float specularTerm = texture(uSpecularTableMap, vec2(specularDot, 0.5)).r * 2.0;
    vec3 specular = texture(uSpecularMap, vSpecularTexCoord).rgb * uSpecularColor.rgb * specularTerm;
#endif

    outColor = vec4((diffuse + specular) * light * vVertexColor, 0.0);
}
)";
