#version 460

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec2 inTexCoord;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 texCoord;

layout (set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
} cam;

struct ObjectData {
    mat4 model;
};

layout (std140, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} obj;

layout (push_constant) uniform PushConstants {
    int modelIndex;
} pcs;

void main()
{
    mat4 model = obj.objects[pcs.modelIndex].model;
    gl_Position = cam.viewproj * model * vec4(inPosition, 1.0f);
    outColor = inColor;
    texCoord = inTexCoord;
}
