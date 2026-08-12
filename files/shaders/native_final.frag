#version 120
uniform sampler2D sceneTexture;
uniform sampler2D weightTexture;
uniform sampler2D bloomTexture;
uniform vec2 inverseSceneSize;
uniform float smaaEnabled;
uniform float bloomEnabled;
uniform float bloomIntensity;

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec3 color = texture2D(sceneTexture, uv).rgb;

    if (smaaEnabled >= 0.5)
    {
        vec4 w = texture2D(weightTexture, uv);
        float hw = clamp(w.x + w.y, 0.0, 0.92);
        float vw = clamp(w.z + w.w, 0.0, 0.92);
        vec3 h = texture2D(sceneTexture, clamp(uv-vec2(inverseSceneSize.x,0.0), vec2(0.0), vec2(1.0))).rgb * w.x
               + texture2D(sceneTexture, clamp(uv+vec2(inverseSceneSize.x,0.0), vec2(0.0), vec2(1.0))).rgb * w.y;
        vec3 v = texture2D(sceneTexture, clamp(uv-vec2(0.0,inverseSceneSize.y), vec2(0.0), vec2(1.0))).rgb * w.z
               + texture2D(sceneTexture, clamp(uv+vec2(0.0,inverseSceneSize.y), vec2(0.0), vec2(1.0))).rgb * w.w;
        float total = hw + vw;
        if (total > 0.0001)
            color = mix(color, (h + v) / total, clamp(total * 0.72, 0.0, 0.82));
    }

    if (bloomEnabled >= 0.5)
        color += max(texture2D(bloomTexture, uv).rgb, vec3(0.0)) * max(bloomIntensity, 0.0);

    gl_FragColor = vec4(max(color, vec3(0.0)), 1.0);
}
