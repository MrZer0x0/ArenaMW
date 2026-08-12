#version 120

uniform sampler2D sceneTexture;
uniform sampler2D depthTexture;
uniform vec2 inverseSceneSize;
uniform mat4 inverseProjectionMatrix;
uniform mat4 projectionMatrix;
uniform float ssrEnabled;
uniform float ssrStrength;
uniform float ssrDistance;

const int SSR_STEPS = 32;
const int SSR_BINARY_STEPS = 5;

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = inverseProjectionMatrix * clip;
    return view.xyz / max(abs(view.w), 1e-6) * sign(view.w);
}

vec3 sampleViewPosition(vec2 uv)
{
    float depth = texture2D(depthTexture, uv).r;
    return reconstructViewPosition(uv, depth);
}

vec2 projectViewPosition(vec3 p)
{
    vec4 clip = projectionMatrix * vec4(p, 1.0);
    if (clip.w <= 1e-5)
        return vec2(-1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

bool insideScreen(vec2 uv)
{
    return uv.x > 0.001 && uv.y > 0.001 && uv.x < 0.999 && uv.y < 0.999;
}

vec3 reconstructNormal(vec2 uv, vec3 p)
{
    vec2 dx = vec2(inverseSceneSize.x, 0.0);
    vec2 dy = vec2(0.0, inverseSceneSize.y);

    vec2 uvL = clamp(uv - dx, vec2(0.0), vec2(1.0));
    vec2 uvR = clamp(uv + dx, vec2(0.0), vec2(1.0));
    vec2 uvD = clamp(uv - dy, vec2(0.0), vec2(1.0));
    vec2 uvU = clamp(uv + dy, vec2(0.0), vec2(1.0));

    vec3 pL = sampleViewPosition(uvL);
    vec3 pR = sampleViewPosition(uvR);
    vec3 pD = sampleViewPosition(uvD);
    vec3 pU = sampleViewPosition(uvU);

    // Pick the derivative that stays on the same depth surface. Central
    // differences across silhouettes were the main source of unstable normals
    // and reflections leaking from one object to another.
    vec3 dxA = pR - p;
    vec3 dxB = p - pL;
    vec3 dyA = pU - p;
    vec3 dyB = p - pD;
    vec3 dX = dot(dxA, dxA) < dot(dxB, dxB) ? dxA : dxB;
    vec3 dY = dot(dyA, dyA) < dot(dyB, dyB) ? dyA : dyB;

    vec3 n = cross(dX, dY);
    float n2 = dot(n, n);
    if (n2 < 1e-10)
        return normalize(-p);
    n *= inversesqrt(n2);
    if (dot(n, -p) < 0.0)
        n = -n;
    return n;
}

float raySceneDelta(vec3 rayPos, vec2 hitUv, out float sceneDepth)
{
    float depth = texture2D(depthTexture, hitUv).r;
    if (depth >= 0.99995)
    {
        sceneDepth = 1e20;
        return 1e20;
    }
    vec3 scenePos = reconstructViewPosition(hitUv, depth);
    sceneDepth = max(-scenePos.z, 0.0);
    float rayDepth = max(-rayPos.z, 0.0);
    return rayDepth - sceneDepth;
}

vec3 sampleReflectionColor(vec2 uv, float blurRadius)
{
    vec2 px = inverseSceneSize * blurRadius;
    vec3 c = texture2D(sceneTexture, uv).rgb * 0.50;
    c += texture2D(sceneTexture, clamp(uv + vec2(px.x, 0.0), vec2(0.0), vec2(1.0))).rgb * 0.125;
    c += texture2D(sceneTexture, clamp(uv - vec2(px.x, 0.0), vec2(0.0), vec2(1.0))).rgb * 0.125;
    c += texture2D(sceneTexture, clamp(uv + vec2(0.0, px.y), vec2(0.0), vec2(1.0))).rgb * 0.125;
    c += texture2D(sceneTexture, clamp(uv - vec2(0.0, px.y), vec2(0.0), vec2(1.0))).rgb * 0.125;
    return c;
}

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec3 color = texture2D(sceneTexture, uv).rgb;
    float depth = texture2D(depthTexture, uv).r;

    if (ssrEnabled >= 0.5 && depth < 0.9998)
    {
        vec3 viewPos = reconstructViewPosition(uv, depth);
        vec3 N = reconstructNormal(uv, viewPos);
        vec3 I = normalize(viewPos);
        vec3 R = normalize(reflect(I, N));

        float nDotV = clamp(dot(N, -I), 0.0, 1.0);
        float fresnel = 0.06 + 0.94 * pow(1.0 - nDotV, 3.0);
        float rayZConfidence = smoothstep(0.045, 0.28, abs(R.z));
        float maxDistance = max(ssrDistance, 64.0);
        float minTravel = max(3.0, abs(viewPos.z) * 0.0012);
        vec3 rayOrigin = viewPos + N * max(0.35, abs(viewPos.z) * 0.00025);

        float prevT = minTravel;
        vec3 prevRayPos = rayOrigin + R * prevT;
        vec2 prevUv = projectViewPosition(prevRayPos);
        float prevSceneDepth = 0.0;
        float prevDelta = 0.0;
        bool prevValid = false;

        if (insideScreen(prevUv))
        {
            prevDelta = raySceneDelta(prevRayPos, prevUv, prevSceneDepth);
            prevValid = prevSceneDepth < 1e19
                && distance(prevUv, uv) > length(inverseSceneSize) * 2.0;
        }

        vec2 hitUv = vec2(-1.0);
        float hitT = 0.0;
        float hit = 0.0;

        for (int i = 1; i <= SSR_STEPS; ++i)
        {
            float f = float(i) / float(SSR_STEPS);
            // More samples close to the source surface, where coarse stepping
            // is most visible, while still reaching the configured distance.
            float t = minTravel + (maxDistance - minTravel) * f * f;
            vec3 rayPos = rayOrigin + R * t;
            vec2 rayUv = projectViewPosition(rayPos);
            if (!insideScreen(rayUv))
                break;

            float sceneDepth = 0.0;
            float delta = raySceneDelta(rayPos, rayUv, sceneDepth);
            bool valid = sceneDepth < 1e19
                && distance(rayUv, uv) > length(inverseSceneSize) * 2.0;

            if (valid && prevValid)
            {
                // View-space depth crossing rather than |rayZ-sceneZ|. The old
                // test accepted unrelated geometry inside a large thickness
                // band, which caused reflections to jump and smear.
                bool crossed = (R.z < 0.0 && prevDelta < 0.0 && delta >= 0.0)
                    || (R.z > 0.0 && prevDelta > 0.0 && delta <= 0.0);

                if (crossed)
                {
                    float lo = prevT;
                    float hi = t;
                    float loDelta = prevDelta;
                    vec2 refinedUv = rayUv;
                    float refinedSceneDepth = sceneDepth;
                    float refinedDelta = delta;

                    for (int b = 0; b < SSR_BINARY_STEPS; ++b)
                    {
                        float mid = 0.5 * (lo + hi);
                        vec3 midPos = rayOrigin + R * mid;
                        vec2 midUv = projectViewPosition(midPos);
                        if (!insideScreen(midUv))
                            break;
                        float midSceneDepth = 0.0;
                        float midDelta = raySceneDelta(midPos, midUv, midSceneDepth);
                        if (midSceneDepth >= 1e19)
                        {
                            lo = mid;
                            continue;
                        }

                        bool onFarSide = (R.z < 0.0) ? (midDelta >= 0.0) : (midDelta <= 0.0);
                        if (onFarSide)
                        {
                            hi = mid;
                            refinedUv = midUv;
                            refinedSceneDepth = midSceneDepth;
                            refinedDelta = midDelta;
                        }
                        else
                        {
                            lo = mid;
                            loDelta = midDelta;
                        }
                    }

                    vec3 refinedRayPos = rayOrigin + R * hi;
                    float refinedRayDepth = max(-refinedRayPos.z, 0.0);
                    float thickness = max(2.0, refinedSceneDepth * 0.0045);
                    if (abs(refinedRayDepth - refinedSceneDepth) <= thickness * 2.0)
                    {
                        hitUv = refinedUv;
                        hitT = hi;
                        hit = 1.0;
                        break;
                    }
                }
            }

            prevT = t;
            prevRayPos = rayPos;
            prevUv = rayUv;
            prevDelta = delta;
            prevSceneDepth = sceneDepth;
            prevValid = valid;
        }

        if (hit > 0.5)
        {
            float edge = min(min(hitUv.x, hitUv.y), min(1.0 - hitUv.x, 1.0 - hitUv.y));
            float edgeFade = smoothstep(0.015, 0.11, edge);
            float travel = clamp(hitT / maxDistance, 0.0, 1.0);
            float distanceFade = 1.0 - smoothstep(0.55, 1.0, travel);
            float selfFade = smoothstep(length(inverseSceneSize) * 2.5,
                length(inverseSceneSize) * 10.0, distance(hitUv, uv));
            float confidence = edgeFade * distanceFade * selfFade * rayZConfidence;

            float blurRadius = 1.0 + travel * 2.0;
            vec3 hitColor = sampleReflectionColor(hitUv, blurRadius);

            // A restrained dielectric-style response makes generic screen-space
            // SSR usable without a material/roughness G-buffer. The strength
            // slider remains the artistic master control.
            float reflectivity = clamp(ssrStrength, 0.0, 1.0)
                * fresnel * confidence;
            reflectivity = min(reflectivity, 0.72);
            color = mix(color, hitColor, reflectivity);
        }
    }

    gl_FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
