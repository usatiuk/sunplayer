#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D uiTexture;

layout(std140, binding = 1) uniform Params {
    vec2 viewportSize;
    vec2 canvasOrigin;
    vec2 canvasSize;
    float sourcePeak;
    float targetPeak;
    float phase;
    float toneMappingEnabled;
    float sdrScale;
    float ndcYUp;
    float linearOutput;
};

const float tau = 6.28318530718;

vec3 srgbToLinear(vec3 value)
{
    bvec3 low = lessThanEqual(value, vec3(0.04045));
    vec3 linearPart = value / 12.92;
    vec3 powerPart = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(powerPart, linearPart, low);
}

vec3 linearToSrgb(vec3 value)
{
    bvec3 low = lessThanEqual(value, vec3(0.0031308));
    vec3 linearPart = value * 12.92;
    vec3 powerPart = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(powerPart, linearPart, low);
}

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
    return toneMap(color) * separator * sdrScale;
}

void main()
{
    vec2 displayUv = ndcYUp > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv;
    vec3 background = srgbToLinear(vec3(17.0, 19.0, 24.0) / 255.0)
        * sdrScale;
    vec3 color = background;

    vec2 pixel = displayUv * viewportSize;
    if (all(greaterThanEqual(pixel, canvasOrigin))
            && all(lessThan(pixel, canvasOrigin + canvasSize))) {
        vec2 canvasUv = (pixel - canvasOrigin) / canvasSize;
        color = pattern(canvasUv);
    }

    vec4 ui = texture(uiTexture, displayUv);
    float alpha = clamp(ui.a, 0.0, 1.0);
    vec3 encodedStraight = alpha > 0.00001
        ? clamp(ui.rgb / alpha, 0.0, 1.0)
        : vec3(0.0);
    vec3 uiLinear = srgbToLinear(encodedStraight)
        * sdrScale;
    color = uiLinear * alpha + color * (1.0 - alpha);
    vec3 outputColor = linearOutput > 0.5
        ? color
        : linearToSrgb(clamp(color, 0.0, 1.0));
    fragColor = vec4(outputColor, 1.0);
}
