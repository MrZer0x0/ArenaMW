#version 120
uniform sampler2D edgeTexture;
uniform vec2 inverseSceneSize;

void main()
{
    vec2 uv = gl_FragCoord.xy * inverseSceneSize;
    vec2 e = texture2D(edgeTexture, uv).rg;
    float left = 0.0, right = 0.0, up = 0.0, down = 0.0;
    if (e.x > 0.0)
    {
        for (int i = 1; i <= 8; ++i)
        {
            float s = texture2D(edgeTexture, clamp(uv-vec2(inverseSceneSize.x*float(i),0.0), vec2(0.0), vec2(1.0))).r;
            if (s < 0.5) break;
            left += 1.0;
        }
        for (int i = 1; i <= 8; ++i)
        {
            float s = texture2D(edgeTexture, clamp(uv+vec2(inverseSceneSize.x*float(i),0.0), vec2(0.0), vec2(1.0))).r;
            if (s < 0.5) break;
            right += 1.0;
        }
    }
    if (e.y > 0.0)
    {
        for (int i = 1; i <= 8; ++i)
        {
            float s = texture2D(edgeTexture, clamp(uv-vec2(0.0,inverseSceneSize.y*float(i)), vec2(0.0), vec2(1.0))).g;
            if (s < 0.5) break;
            down += 1.0;
        }
        for (int i = 1; i <= 8; ++i)
        {
            float s = texture2D(edgeTexture, clamp(uv+vec2(0.0,inverseSceneSize.y*float(i)), vec2(0.0), vec2(1.0))).g;
            if (s < 0.5) break;
            up += 1.0;
        }
    }
    float hsum = left + right + 1.0;
    float vsum = up + down + 1.0;
    vec4 w = vec4(right/hsum, left/hsum, up/vsum, down/vsum);
    w *= 0.72;
    gl_FragColor = w;
}
