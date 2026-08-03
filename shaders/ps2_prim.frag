#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Vertex-colored geometry (no texture sampling yet — textures in Milestone 9+)
    outColor = fragColor;
}
