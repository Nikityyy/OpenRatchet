#version 450

// PS2 vertex: position in NDC, RGBA color, UV
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    // Position already in clip space (PS2 uses its own projection)
    gl_Position = inPosition;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
