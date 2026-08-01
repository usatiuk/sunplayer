#version 440

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D videoTexture;
layout(binding = 1) uniform sampler2D uiTexture;

layout(std140, binding = 2) uniform CompositorParams {
    vec2 viewportSize;
    vec2 videoOrigin;
    vec2 videoSize;
    float sdrScale;
    float ndcYUp;
    float linearOutput;
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

    vec4 ui = texture(uiTexture, displayUv);
    float alpha = clamp(ui.a, 0.0, 1.0);
    vec3 encodedStraight = alpha > 0.00001
        ? clamp(ui.rgb / alpha, 0.0, 1.0)
        : vec3(0.0);
    vec3 uiLinear = srgbToLinear(encodedStraight);
    color = uiLinear * alpha + color * (1.0 - alpha);
    color *= sdrScale;
    vec3 outputColor = linearOutput > 0.5
        ? color
        : linearToSrgb(clamp(color, 0.0, 1.0));
    fragColor = vec4(outputColor, 1.0);
}
