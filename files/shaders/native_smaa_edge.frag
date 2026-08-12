#version 120
uniform sampler2D sceneTexture;
uniform sampler2D depthTexture;
uniform vec2 inverseSceneSize;
uniform float smaaThreshold;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec2 dx = vec2(inverseSceneSize.x, 0.0);
    vec2 dy = vec2(0.0, inverseSceneSize.y);
    float c = luma(texture2D(sceneTexture, uv).rgb);
    float l = luma(texture2D(sceneTexture, clamp(uv-dx, vec2(0.0), vec2(1.0))).rgb);
    float t = luma(texture2D(sceneTexture, clamp(uv-dy, vec2(0.0), vec2(1.0))).rgb);
    float d = texture2D(depthTexture, uv).r;
    float dl = texture2D(depthTexture, clamp(uv-dx, vec2(0.0), vec2(1.0))).r;
    float dt = texture2D(depthTexture, clamp(uv-dy, vec2(0.0), vec2(1.0))).r;
    float threshold = clamp(smaaThreshold, 0.02, 0.35);
    float ex = max(abs(c-l) / max(max(c,l), 0.08), abs(d-dl) * 18.0);
    float ey = max(abs(c-t) / max(max(c,t), 0.08), abs(d-dt) * 18.0);
    gl_FragColor = vec4(step(threshold, ex), step(threshold, ey), 0.0, 1.0);
}
