#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D videoTexture;
layout(binding = 1) uniform sampler2D subtitleTexture;
layout(binding = 2) uniform sampler2D uiTexture;

layout(std140, binding = 3) uniform CompositorParams {
    vec2 viewportSize;
    vec2 videoOrigin;
    vec2 videoSize;
    float sdrScale;
    float ndcYUp;
    float outputEncoding;
    float subtitleOpacity;
};

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

vec3 linearToGamma22(vec3 value)
{
    return pow(value, vec3(1.0 / 2.2));
}

vec3 linearSrgbToBt2020(vec3 value)
{
    return vec3(
        dot(value, vec3(0.627404, 0.329283, 0.043313)),
        dot(value, vec3(0.069097, 0.919540, 0.011362)),
        dot(value, vec3(0.016391, 0.088013, 0.895595)));
}

vec3 linearToPq(vec3 value)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    const float referenceWhiteNits = 203.0;
    const float pqPeakNits = 10000.0;

    vec3 normalized = clamp(
        value * (referenceWhiteNits / pqPeakNits), 0.0, 1.0);
    vec3 powered = pow(normalized, vec3(m1));
    return pow(
        (vec3(c1) + c2 * powered)
            / (vec3(1.0) + c3 * powered),
        vec3(m2));
}

vec3 compositeSrgbPremultiplied(
    vec3 background, vec4 layer, float brightness, float layerOpacity)
{
    float alpha = clamp(layer.a, 0.0, 1.0);
    vec3 encodedStraight = alpha > 0.00001
        ? clamp(layer.rgb / alpha, 0.0, 1.0)
        : vec3(0.0);
    vec3 linear = srgbToLinear(encodedStraight);
    float effectiveAlpha = alpha * clamp(layerOpacity, 0.0, 1.0);
    return linear * (brightness * effectiveAlpha)
        + background * (1.0 - effectiveAlpha);
}

void main()
{
    vec2 displayUv = ndcYUp > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv;
    vec3 color = vec3(0.0);

    vec2 pixel = displayUv * viewportSize;
    if (all(greaterThanEqual(pixel, videoOrigin))
            && all(lessThan(pixel, videoOrigin + videoSize))) {
        vec2 videoUv = (pixel - videoOrigin) / videoSize;
        color = texture(videoTexture, videoUv).rgb;
    }

    color = compositeSrgbPremultiplied(
        color, texture(subtitleTexture, displayUv), 0.8, subtitleOpacity);
    color = compositeSrgbPremultiplied(
        color, texture(uiTexture, displayUv), 1.0, 1.0);
    color *= sdrScale;
    vec3 encodedColor = clamp(color, 0.0, 1.0);
    vec3 outputColor;
    if (outputEncoding > 2.5) {
        outputColor = linearToPq(
            max(linearSrgbToBt2020(color), vec3(0.0)));
    } else if (outputEncoding > 1.5) {
        outputColor = color;
    } else if (outputEncoding > 0.5) {
        outputColor = linearToGamma22(encodedColor);
    } else {
        outputColor = linearToSrgb(encodedColor);
    }
    fragColor = vec4(outputColor, 1.0);
}
