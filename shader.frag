#version 460

layout (location = 0) in vec3 inColor;
layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 1) uniform SceneData {
    vec4 fogColor;
    vec4 fogDistance;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} scene;

void main()
{
    outFragColor = vec4(inColor + scene.ambientColor.xyz, 1.0f);
}
