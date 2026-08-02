#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform DiagnosticVideoParams {
    float sourcePeak;
    float targetPeak;
    float phase;
    float toneMappingEnabled;
    float canonicalYFlip;
};

const float tau = 6.28318530718;

vec3 spectrum(float position)
{
    return 0.5 + 0.5 * cos(tau * (position + vec3(0.0, 0.33, 0.67)));
}

float toneMapSignal(float signal)
{
    if (toneMappingEnabled < 0.5)
        return signal;

    float normalized = signal / targetPeak;
    float whitePoint = max(sourcePeak / targetPeak, 1.0);
    float mapped = normalized
        * (1.0 + normalized / (whitePoint * whitePoint))
        / (1.0 + normalized);
    return targetPeak * mapped;
}

vec3 toneMap(vec3 color)
{
    float signal = max(color.r, max(color.g, color.b));
    return signal > 0.0 ? color * (toneMapSignal(signal) / signal) : color;
}

vec3 pattern(vec2 patternUv)
{
    float ramp = mix(0.02, sourcePeak, smoothstep(0.0, 1.0, patternUv.x));
    vec3 color;
    if (patternUv.y < 0.33) {
        color = vec3(ramp);
    } else if (patternUv.y < 0.66) {
        color = spectrum(patternUv.x + phase) * ramp;
    } else {
        float stepValue = floor(patternUv.x * 8.0) / 7.0;
        color = vec3(mix(0.0, sourcePeak, stepValue));
    }

    float separator =
        step(0.008, abs(patternUv.y - 0.33))
        * step(0.008, abs(patternUv.y - 0.66));
    return toneMap(color) * separator;
}

void main()
{
    vec2 patternUv = canonicalYFlip > 0.5
        ? vec2(uv.x, 1.0 - uv.y)
        : uv;
    fragColor = vec4(pattern(patternUv), 1.0);
}
