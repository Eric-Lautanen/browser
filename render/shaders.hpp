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
out vec4 FragColor;
void main() {
    if (uUseTexture == 0) { FragColor = vColor; }
    else if (uTextureIsRGBA == 1) { vec4 t = texture(uTexture, vTexCoord); FragColor = vec4(vColor.rgb * t.rgb, vColor.a * t.a); }
    else if (uUseSDF == 1) { float d = texture(uTexture, vTexCoord).r; float w = min(fwidth(d), 0.2); float a = smoothstep(0.5 - w, 0.5 + w, d); FragColor = vec4(vColor.rgb, vColor.a * a); }
    else { float a = texture(uTexture, vTexCoord).r; FragColor = vec4(vColor.rgb, vColor.a * a); }
}
)";

}
