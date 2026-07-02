#pragma once
#include <string>
namespace browser::render {

constexpr const char* BASIC_VERTEX_SHADER = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uProjection;
out vec4 vColor;
out vec2 vTexCoord;
void main() { gl_Position = uProjection * vec4(aPos, 0.0, 1.0); vColor = aColor; vTexCoord = aTexCoord; }
)";

constexpr const char* BASIC_FRAGMENT_SHADER = R"(
#version 330 core
in vec4 vColor;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform int uUseTexture;
uniform int uTextureIsRGBA;
uniform int uUseSDF;
uniform int uFilterActive;
uniform float uFilterBrightness;
uniform float uFilterContrast;
uniform float uFilterGrayscale;
uniform float uFilterInvert;
uniform float uFilterSepia;
uniform float uFilterSaturate;
uniform float uFilterHueRotate;
uniform float uFilterOpacity;
out vec4 FragColor;

vec3 apply_filter(vec3 col) {
    // brightness
    col = col * uFilterBrightness;
    // contrast
    col = (col - 0.5) * uFilterContrast + 0.5;
    // saturate
    float gray = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(gray), col, uFilterSaturate);
    // grayscale
    col = mix(col, vec3(gray), uFilterGrayscale);
    // sepia
    vec3 sepia = vec3(
        dot(col, vec3(0.393, 0.769, 0.189)),
        dot(col, vec3(0.349, 0.686, 0.168)),
        dot(col, vec3(0.272, 0.534, 0.131))
    );
    col = mix(col, sepia, uFilterSepia);
    // hue-rotate (simplified: approximate using sin/cos matrix)
    float angle = radians(uFilterHueRotate);
    float c = cos(angle);
    float s = sin(angle);
    mat3 hueMat = mat3(
        0.299 + 0.701*c + 0.168*s, 0.587 - 0.587*c + 0.330*s, 0.114 - 0.114*c - 0.497*s,
        0.299 - 0.299*c - 0.328*s, 0.587 + 0.413*c + 0.035*s, 0.114 - 0.114*c + 0.293*s,
        0.299 - 0.300*c + 1.250*s, 0.587 - 0.588*c - 1.050*s, 0.114 + 0.886*c - 0.203*s
    );
    col = col * hueMat;
    // invert
    col = mix(col, vec3(1.0) - col, uFilterInvert);
    // opacity is handled via alpha
    return col;
}

void main() {
    if (uUseTexture == 0) {
        FragColor = vColor;
    } else if (uTextureIsRGBA == 1) {
        vec4 t = texture(uTexture, vTexCoord);
        FragColor = vec4(vColor.rgb * t.rgb, vColor.a * t.a);
    } else if (uUseSDF == 1) {
        float d = texture(uTexture, vTexCoord).r;
        float w = min(fwidth(d), 0.2);
        float a = smoothstep(0.5 - w, 0.5 + w, d);
        FragColor = vec4(vColor.rgb, vColor.a * a);
    } else {
        float a = texture(uTexture, vTexCoord).r;
        FragColor = vec4(vColor.rgb, vColor.a * a);
    }
    if (uFilterActive == 1) {
        FragColor.rgb = apply_filter(FragColor.rgb);
        FragColor.a *= uFilterOpacity;
    }
}
)";

// Single-pass Gaussian blur fragment shader
constexpr const char* BLUR_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vTexCoord;
uniform sampler2D uBlurTexture;
uniform float uBlurRadius;
out vec4 FragColor;

float gauss(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    vec2 texSize = textureSize(uBlurTexture, 0);
    vec2 step = vec2(1.0 / texSize.x, 1.0 / texSize.y);
    float sigma = max(uBlurRadius / 2.0, 0.5);
    int radius = int(ceil(uBlurRadius));
    vec4 sum = vec4(0.0);
    float totalWeight = 0.0;
    for (int dx = -radius; dx <= radius; dx++) {
        for (int dy = -radius; dy <= radius; dy++) {
            float w = gauss(float(dx), sigma) * gauss(float(dy), sigma);
            vec2 offset = vec2(float(dx) * step.x, float(dy) * step.y);
            sum += texture(uBlurTexture, vTexCoord + offset) * w;
            totalWeight += w;
        }
    }
    FragColor = sum / totalWeight;
}
)";

}
