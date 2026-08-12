// ArenaMW compatibility port of the useful material-lighting ideas from
// Enhanced PBR Lighting (OpenMW 0.49-0.51).  This deliberately keeps the
// existing ArenaMW 0.47 lighting, HDR/Bloom and material pipeline intact and
// layers the PBR response on top of it through runtime uniforms.
#ifndef ARENAMW_ENHANCED_PBR_LIGHTING_GLSL
#define ARENAMW_ENHANCED_PBR_LIGHTING_GLSL

uniform float pbrEnhancedLighting;
uniform float pbrDiffuseResponse;
uniform float pbrObjectRoughness;
uniform float pbrTerrainRoughness;
uniform float pbrSpecularStrength;
uniform float pbrAmbientStrength;
uniform float pbrSubsurfaceStrength;

float arenaPbrPow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float arenaPbrBurleyDiffuse(float roughness, float nDotL, float lDotH, float nDotV)
{
    float energyBias = roughness * 0.5;
    float f90 = energyBias + 2.0 * lDotH * lDotH * roughness;
    float lightScatter = 1.0 + (f90 - 1.0) * arenaPbrPow5(1.0 - nDotL);
    float viewScatter = 1.0 + (f90 - 1.0) * arenaPbrPow5(1.0 - nDotV);
    return lightScatter * viewScatter;
}

// Liam/Wareya-style idea for non-PBR assets: infer only a *small* amount of
// material variation from the base colour instead of pretending arbitrary
// legacy spec maps are metal/roughness/AO parameter textures. The estimate is
// deliberately bounded by the user roughness control and never generates AO,
// metalness or SSS, which keeps old Morrowind interiors stable across mip LODs.
float arenaPbrEstimateLegacyRoughness(vec3 albedo, float baseRoughness, float interiorFactor)
{
    vec3 c = clamp(albedo, vec3(0.0), vec3(1.0));
    float brightness = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float chroma = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));

    // Keep the estimate broad and matte: coloured/bright detail changes the
    // lobe slightly, while the user slider remains the primary control.
    float materialCue = clamp(chroma * 0.55 + abs(brightness - 0.5) * 0.35, 0.0, 1.0);
    float estimated = mix(0.86, 0.62, materialCue);
    float floorValue = mix(0.46, 0.60, clamp(interiorFactor, 0.0, 1.0));
    estimated = max(estimated, floorValue);
    return clamp(mix(baseRoughness, estimated, 0.22), floorValue, 1.0);
}

vec3 arenaPbrEnvironmentBrdf(vec3 f0, float roughness, float nDotV)
{
    // Epic's split-sum approximation, also used by Enhanced PBR Lighting.
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

vec3 arenaPbrF0(vec3 albedo, float metallicity)
{
    // ArenaMW's legacy material path is not consistently linear before the
    // final HDR conversion, so keep this conservative and avoid another gamma
    // transform here.  This preserves existing material colours.
    vec3 safeAlbedo = clamp(albedo, vec3(0.0), vec3(1.0));
    return mix(vec3(0.04), safeAlbedo, clamp(metallicity, 0.0, 1.0));
}

float arenaPbrSubsurfaceTerm(float nDotV, vec3 N, vec3 V, vec3 L)
{
    float sss = 1.0 - nDotV;
    sss = sss * sss * 0.5
        + sqrt(sqrt(sqrt(abs(dot(N, L)) * 0.9 + 0.1)))
        * exp(-1.7 * (1.0 - pow(max(0.0, dot(L, -V)), 1.64)));
    return clamp(sss, 0.0, 1.0);
}

vec3 arenaPbrDirectSpecular(vec3 lightColor, vec3 N, vec3 V, vec3 L,
    float roughness, float metallicity, vec3 albedo)
{
    float nDotL = max(dot(N, L), 0.0);
    float nDotV = max(dot(N, V), 0.0);
    if (nDotL <= 0.0 || nDotV <= 0.0)
        return vec3(0.0);

    vec3 H = normalize(V + L);
    float nDotH = max(dot(N, H), 0.0);
    float vDotH = max(dot(V, H), 0.0);
    float safeRoughness = clamp(roughness, 0.08, 1.0);

    vec3 f0 = arenaPbrF0(albedo, metallicity);
    float D = pbrDistributionGGX(nDotH, safeRoughness);
    float G = pbrGeometrySmith(nDotV, nDotL, safeRoughness);
    vec3 F = pbrFresnelSchlick(vDotH, f0);
    vec3 spec = (D * G * F) / max(4.0 * nDotV * nDotL, 1e-4);

    // Direct-light energy only.  Environment response is handled separately.
    return spec * lightColor * nDotL;
}

vec3 arenaPbrAccumulatePointSpecular(vec3 viewPos, vec3 N, vec3 V,
    float roughness, float metallicity, vec3 albedo)
{
    vec3 result = vec3(0.0);
    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        int lightIndex = PointLightIndex[i];
#else
        int lightIndex = i;
#endif
        vec3 toLight = lcalcPosition(lightIndex) - viewPos;
        float lightDistance = length(toLight);
#if !@lightingMethodFFP
        if (lightDistance > lcalcRadius(lightIndex) * 2.0)
            continue;
#endif
        if (lightDistance <= 0.0001)
            continue;

        vec3 L = toLight / lightDistance;
        float illumination = lcalcIllumination(lightIndex, lightDistance);
        if (illumination <= 0.0001)
            continue;

        vec3 lightColor = max(lcalcDiffuse(lightIndex).xyz, vec3(0.0)) * illumination;
        result += arenaPbrDirectSpecular(lightColor, N, V, L,
            roughness, metallicity, albedo);
    }
    return result;
}

vec3 arenaPbrDiffuseForLight(vec3 lightColor, vec3 N, vec3 V, vec3 L,
    float roughness, float metallicity, vec3 albedo, float diffuseResponse)
{
    float nDotL = max(dot(N, L), 0.0);
    float nDotV = max(dot(N, V), 0.0);
    if (nDotL <= 0.0001 || nDotV <= 0.0001)
        return vec3(0.0);

    vec3 H = normalize(V + L);
    float lDotH = max(dot(L, H), 0.0);
    vec3 f0 = arenaPbrF0(albedo, metallicity);
    vec3 F = pbrFresnelSchlick(max(dot(H, V), 0.0), f0);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallicity);
    float burley = arenaPbrBurleyDiffuse(roughness, nDotL, lDotH, nDotV);
    float diffuseMix = clamp(diffuseResponse, 0.0, 1.0);
    float diffuseBrdf = mix(1.0, clamp(burley, 0.55, 1.45), diffuseMix);
    return max(lightColor, vec3(0.0)) * nDotL * kd * diffuseBrdf;
}

void arenaApplyEnhancedPbr(vec3 viewPos, vec3 viewNormal, vec3 albedo,
    float roughness, float metallicity, float ambientOcclusion, float sssMask,
    float shadowing, float legacyMaterialFactor, float interiorFactor,
    inout vec3 diffuseLight, inout vec3 ambientLight,
    out vec3 enhancedSpecular)
{
    enhancedSpecular = vec3(0.0);
    if (pbrEnhancedLighting < 0.5)
        return;

    vec3 N = normalize(viewNormal);
    vec3 V = normalize(-viewPos);
    float legacySafety = clamp(legacyMaterialFactor, 0.0, 1.0);
    float interiorSafety = clamp(interiorFactor, 0.0, 1.0);
    float legacyMinRoughness = mix(0.08, mix(0.46, 0.60, interiorSafety), legacySafety);
    roughness = clamp(roughness, 0.08, 1.0);
    if (legacySafety > 0.001)
        roughness = mix(roughness, arenaPbrEstimateLegacyRoughness(albedo, roughness, interiorSafety), legacySafety);
    roughness = pbrFilterRoughness(N, max(roughness, legacyMinRoughness));

    // Legacy content has no trustworthy metalness/AO convention. Liam's PBR
    // has a separate fake-PBR path for this exact reason; ArenaMW follows the
    // same principle and never derives dark AO or metalness from old spec maps.
    metallicity = clamp(metallicity, 0.0, 1.0) * (1.0 - legacySafety);
    ambientOcclusion = mix(clamp(ambientOcclusion, 0.0, 1.0), 1.0, legacySafety);

    // Rebuild the direct-light accumulator with the PBR BRDF.  The previous
    // compatibility port only multiplied OpenMW's already-computed Lambert
    // result, which made the PBR toggle almost invisible.  We intentionally
    // keep OpenMW's attenuation and light list, but replace the surface BRDF.
    vec3 directPbr = vec3(0.0);
    // Wareya/Liam default to Lambert on non-PBR-authored assets. Preserve the
    // user's Burley control, but strongly reduce it for legacy materials so
    // cloth/wood/old vertex normals do not gain an artificial coated look.
    float effectiveDiffuseResponse = clamp(pbrDiffuseResponse, 0.0, 1.0)
        * mix(1.0, 0.18, legacySafety);
    vec3 L = normalize(lcalcPosition(0));
    vec3 sunDiffuse = max(lcalcDiffuse(0).xyz, vec3(0.0));
    directPbr += arenaPbrDiffuseForLight(sunDiffuse, N, V, L,
        roughness, metallicity, albedo, effectiveDiffuseResponse) * shadowing;

    vec3 sunColor = max(lcalcSpecular(0).xyz, sunDiffuse * 0.30);
    enhancedSpecular += arenaPbrDirectSpecular(sunColor, N, V, L,
        roughness, metallicity, albedo) * shadowing;

    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        int lightIndex = PointLightIndex[i];
#else
        int lightIndex = i;
#endif
        vec3 toLight = lcalcPosition(lightIndex) - viewPos;
        float lightDistance = length(toLight);
        if (lightDistance <= 0.0001)
            continue;

        float illumination = lcalcIllumination(lightIndex, lightDistance);
        if (illumination <= 0.0001)
            continue;

        vec3 pointL = toLight / lightDistance;
        vec3 pointDiffuse = max(lcalcDiffuse(lightIndex).xyz, vec3(0.0)) * illumination;
        directPbr += arenaPbrDiffuseForLight(pointDiffuse, N, V, pointL,
            roughness, metallicity, albedo, effectiveDiffuseResponse);
        enhancedSpecular += arenaPbrDirectSpecular(pointDiffuse, N, V, pointL,
            roughness, metallicity, albedo);
    }

    // Use the rebuilt PBR result as the primary direct lighting.  A small
    // legacy contribution prevents authored Morrowind materials from becoming
    // unexpectedly black when a mod has unusual light/material data.
    float legacyPreserve = mix(0.12, mix(0.38, 0.55, interiorSafety), legacySafety);
    diffuseLight = mix(directPbr, diffuseLight, legacyPreserve);

    float nDotV = max(dot(N, V), 0.0);
    vec3 f0 = arenaPbrF0(albedo, metallicity);
    vec3 envBrdf = arenaPbrEnvironmentBrdf(f0, roughness, nDotV);
    vec3 envRadiance = max(gl_LightModel.ambient.xyz, vec3(0.0))
        + sunDiffuse * 0.16;

    // AO and roughness now visibly affect the ambient term even without packed
    // material maps, while metals retain more environment response.
    vec3 pbrAmbient = ambientLight * ambientOcclusion * (1.0 - metallicity * 0.45);
    float environmentSpecularWeight = mix(1.0, 0.06, legacySafety);
    pbrAmbient += envRadiance * envBrdf * ambientOcclusion
        * (0.30 + 0.70 * (1.0 - roughness)) * environmentSpecularWeight;
    float safeAmbientStrength = clamp(pbrAmbientStrength, 0.0, 1.0) * mix(1.0, mix(0.90, 0.75, interiorSafety), legacySafety);
    ambientLight = mix(ambientLight, pbrAmbient, safeAmbientStrength);
    if (pbrAmbientStrength > 1.0)
        ambientLight += pbrAmbient * (pbrAmbientStrength - 1.0) * 0.35;

    float specularScale = clamp(pbrSpecularStrength, 0.0, 2.5)
        * mix(1.0, mix(0.55, 0.35, interiorSafety), legacySafety);
    enhancedSpecular *= specularScale;
    enhancedSpecular = min(enhancedSpecular, mix(vec3(3.5), mix(vec3(1.2), vec3(0.75), interiorSafety), legacySafety));

    float sssStrength = clamp(pbrSubsurfaceStrength, 0.0, 1.5)
        * clamp(sssMask, 0.0, 1.0) * (1.0 - metallicity);
    if (sssStrength > 0.0001)
    {
        float sss = arenaPbrSubsurfaceTerm(nDotV, N, V, L);
        vec3 sssLight = sunDiffuse * sss * sssStrength;
        diffuseLight += sssLight * (0.22 + 0.78 * shadowing);
    }
}
#endif
