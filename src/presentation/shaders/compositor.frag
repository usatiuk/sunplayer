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
    float subtitleBrightness;
    float padding;
    vec4 outputRow0;
    vec4 outputRow1;
    vec4 outputRow2;
};

vec3 srgbToLinear(vec3 value)
{
    bvec3 low = lessThanEqual(value, vec3(0.04045));
    vec3 linearPart = value / 12.92;
    vec3 powerPart = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(powerPart, linearPart, low);
}

// @LIBPLACEBO_TRANSFER_FUNCTIONS@

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
        color, texture(subtitleTexture, displayUv), subtitleBrightness, subtitleOpacity);
    color = compositeSrgbPremultiplied(
        color, texture(uiTexture, displayUv), 1.0, 1.0);
    color *= sdrScale;
    // Preserve signed wide colors until conversion into the declared basis.
    color = vec3(dot(outputRow0.xyz, color), dot(outputRow1.xyz, color), dot(outputRow2.xyz, color));
    vec3 outputColor;
    if (outputEncoding > 2.5) {
        outputColor = linearToPq(clamp(color, 0.0, 10000.0 / 203.0));
    } else if (outputEncoding > 1.5) {
        outputColor = color;
    } else if (outputEncoding > 0.5) {
        outputColor = linearToGamma22(clamp(color, 0.0, 1.0));
    } else {
        outputColor = linearToSrgb(clamp(color, 0.0, 1.0));
    }
    fragColor = vec4(outputColor, 1.0);
}
