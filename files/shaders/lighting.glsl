#include "lighting_pbr_compat.glsl"

#if @hdrLighting
#include "lighting_util.glsl"

// Bloom и glow параметры (можно настроить через uniforms или defines)
#ifndef BLOOM_STRENGTH
#define BLOOM_STRENGTH 0.22  // Усиленное, но контролируемое свечение ярких источников
#endif

#ifndef GLOW_INTENSITY
#define GLOW_INTENSITY 0.68  // Более заметный ореол вокруг локальных источников
#endif

#ifndef GLOW_RADIUS
#define GLOW_RADIUS 2.25  // Расширенный визуальный радиус ореола
#endif

// Отдельные настройки для точечных источников
#ifndef POINT_BLOOM_MULTIPLIER
#define POINT_BLOOM_MULTIPLIER 0.42  // Ярче факелы, лампы и свечи
#endif

#ifndef POINT_GLOW_MULTIPLIER
#define POINT_GLOW_MULTIPLIER 0.75  // Сильнее локальное свечение источников
#endif

// Переменная для накопления bloom эффекта
vec3 bloomAccumulator = vec3(0.0);

// Функция для вычисления bloom вклада от источника света
vec3 calculateBloom(vec3 lightColor, float lightIntensity, float distance, float radius, float multiplier)
{
    float bloomFactor = lightIntensity * smoothstep(radius * 2.0, 0.0, distance);
    bloomFactor = pow(bloomFactor, 2.5);  // Более резкое затухание
    return lightColor * bloomFactor * BLOOM_STRENGTH * multiplier;
}

// Функция для вычисления glow эффекта
vec3 calculateGlow(vec3 lightColor, float distance, float radius, float multiplier)
{
    float glowFactor = smoothstep(radius * GLOW_RADIUS, 0.0, distance);
    glowFactor = pow(glowFactor, 2.0);  // Более мягкое затухание
    return lightColor * glowFactor * GLOW_INTENSITY * 0.30 * multiplier;
}

void perLightSun(out vec3 diffuseOut, vec3 viewPos, vec3 viewNormal)
{
    vec3 lightDir = normalize(lcalcPosition(0));
    float lambert = dot(viewNormal.xyz, lightDir);

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    vec3 sunDiffuse = lcalcDiffuse(0).xyz * lambert * 0.7;
    diffuseOut = sunDiffuse;
    
    // Bloom для яркого солнечного света
    float sunBloom = max(lambert - 0.8, 0.0) * 5.0;
    bloomAccumulator += sunDiffuse * sunBloom * BLOOM_STRENGTH * 0.5;
}

void perLightPoint(out vec3 ambientOut, out vec3 diffuseOut, int lightIndex, vec3 viewPos, vec3 viewNormal)
{
    vec3 lightPos = lcalcPosition(lightIndex) - viewPos;
    float lightDistance = length(lightPos);

// cull non-FFP point lighting by radius, light is guaranteed to not fall outside this bound with our cutoff
#if !@lightingMethodFFP
    float radius = lcalcRadius(lightIndex);

    // Расширяем радиус проверки для glow эффекта
    if (lightDistance > radius * GLOW_RADIUS * 2.0)
    {
        ambientOut = vec3(0.0);
        diffuseOut = vec3(0.0);
        return;
    }
#endif

    lightPos = normalize(lightPos);

    float illumination = lcalcIllumination(lightIndex, lightDistance);
    vec3 lightDiffuse = lcalcDiffuse(lightIndex);
    
    ambientOut = lcalcAmbient(lightIndex) * illumination;
    float lambert = dot(viewNormal.xyz, lightPos) * illumination;

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    // Основное освещение
    diffuseOut = lightDiffuse * lambert * 0.78;
    
#if !@lightingMethodFFP
    // Добавляем glow эффект (мягкое свечение вокруг источника) - снижено для интерьеров
    vec3 glowContribution = calculateGlow(lightDiffuse, lightDistance, radius, POINT_GLOW_MULTIPLIER);
    diffuseOut += glowContribution;
    
    // Bloom от точечных источников света - снижено для интерьеров
    vec3 bloomContribution = calculateBloom(lightDiffuse, illumination * lambert, lightDistance, radius, POINT_BLOOM_MULTIPLIER);
    bloomAccumulator += bloomContribution;
#endif
}

#if PER_PIXEL_LIGHTING
void doLighting(vec3 viewPos, vec3 viewNormal, float shadowing, out vec3 diffuseLight, out vec3 ambientLight)
#else
void doLighting(vec3 viewPos, vec3 viewNormal, out vec3 diffuseLight, out vec3 ambientLight, out vec3 shadowDiffuse)
#endif
{
    vec3 ambientOut, diffuseOut;
    
    // Сброс аккумулятора bloom
    bloomAccumulator = vec3(0.0);

    perLightSun(diffuseOut, viewPos, viewNormal);
    ambientLight = gl_LightModel.ambient.xyz;
#if PER_PIXEL_LIGHTING
    // Ambient lift для осветления теней - делаем тени менее глубокими
    float ambientLift = 0.15;
    diffuseLight = diffuseOut * max(shadowing, ambientLift);
#else
    shadowDiffuse = diffuseOut;
    diffuseLight = vec3(0.0);
#endif

    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        perLightPoint(ambientOut, diffuseOut, PointLightIndex[i], viewPos, viewNormal);
#else
        perLightPoint(ambientOut, diffuseOut, i, viewPos, viewNormal);
#endif
        ambientLight += ambientOut;
        diffuseLight += diffuseOut;
    }
    
    // Добавляем накопленный bloom к финальному освещению
    diffuseLight += bloomAccumulator;
}

vec3 getSpecular(vec3 viewNormal, vec3 viewDirection, float shininess, vec3 matSpec)
{
    vec3 lightDir = normalize(lcalcPosition(0));
#if @materialQuality >= 4
    vec3 viewDir = normalize(-viewDirection);
    return pbrSunSpecular(normalize(viewNormal), viewDir, lightDir, shininess,
        lcalcSpecular(0).xyz * matSpec);
#else
    float NdotL = dot(viewNormal, lightDir);
    if (NdotL <= 0.0)
        return vec3(0.0);
    vec3 halfVec = normalize(lightDir - viewDirection);
    float NdotH = dot(viewNormal, halfVec);
    return pow(max(NdotH, 0.0), max(1e-4, shininess))
        * lcalcSpecular(0).xyz * matSpec * 0.35;
#endif
}

#else
#include "lighting_util.glsl"

void perLightSun(out vec3 diffuseOut, vec3 viewPos, vec3 viewNormal)
{
    vec3 lightDir = normalize(lcalcPosition(0));
    float lambert = dot(viewNormal.xyz, lightDir);

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    diffuseOut = lcalcDiffuse(0).xyz * lambert;
}

void perLightPoint(out vec3 ambientOut, out vec3 diffuseOut, int lightIndex, vec3 viewPos, vec3 viewNormal)
{
    vec3 lightPos = lcalcPosition(lightIndex) - viewPos;
    float lightDistance = length(lightPos);

// cull non-FFP point lighting by radius, light is guaranteed to not fall outside this bound with our cutoff
#if !@lightingMethodFFP
    float radius = lcalcRadius(lightIndex);

    if (lightDistance > radius * POINT_LIGHT_COVERAGE_MULTIPLIER * 2.0)
    {
        ambientOut = vec3(0.0);
        diffuseOut = vec3(0.0);
        return;
    }
#endif

    lightPos = normalize(lightPos);

    float illumination = lcalcIllumination(lightIndex, lightDistance);
    ambientOut = lcalcAmbient(lightIndex) * illumination;
    float lambert = dot(viewNormal.xyz, lightPos) * illumination;

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    diffuseOut = lcalcDiffuse(lightIndex) * lambert;
}

#if PER_PIXEL_LIGHTING
void doLighting(vec3 viewPos, vec3 viewNormal, float shadowing, out vec3 diffuseLight, out vec3 ambientLight)
#else
void doLighting(vec3 viewPos, vec3 viewNormal, out vec3 diffuseLight, out vec3 ambientLight, out vec3 shadowDiffuse)
#endif
{
    vec3 ambientOut, diffuseOut;

    perLightSun(diffuseOut, viewPos, viewNormal);
    ambientLight = gl_LightModel.ambient.xyz;
#if PER_PIXEL_LIGHTING
    diffuseLight = diffuseOut * shadowing;
#else
    shadowDiffuse = diffuseOut;
    diffuseLight = vec3(0.0);
#endif

    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        perLightPoint(ambientOut, diffuseOut, PointLightIndex[i], viewPos, viewNormal);
#else
        perLightPoint(ambientOut, diffuseOut, i, viewPos, viewNormal);
#endif
        ambientLight += ambientOut;
        diffuseLight += diffuseOut;
    }
}

vec3 getSpecular(vec3 viewNormal, vec3 viewDirection, float shininess, vec3 matSpec)
{
    vec3 lightDir = normalize(lcalcPosition(0));
#if @materialQuality >= 4
    vec3 viewDir = normalize(-viewDirection);
    return pbrSunSpecular(normalize(viewNormal), viewDir, lightDir, shininess,
        lcalcSpecular(0).xyz * matSpec);
#else
    float NdotL = dot(viewNormal, lightDir);
    if (NdotL <= 0.0)
        return vec3(0.0);
    vec3 halfVec = normalize(lightDir - viewDirection);
    float NdotH = dot(viewNormal, halfVec);
    return pow(max(NdotH, 0.0), max(1e-4, shininess))
        * lcalcSpecular(0).xyz * matSpec * 0.35;
#endif
}

#endif
