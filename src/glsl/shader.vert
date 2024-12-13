#version 460

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;

layout (set = 0, binding = 0) uniform CameraData {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
} cam;

struct ObjectData {
    mat4 model;
    mat4 normalMat;
};

layout (set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} obj;

layout (push_constant) uniform PushConstants {
    int modelIndex;
} pcs;

layout (location = 0) out vec3 outFragPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outTexCoord;

void main()
{
    ObjectData object = obj.objects[pcs.modelIndex];
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    outFragPos = vec3(worldPos);
    outNormal = mat3(object.normalMat) * inNormal;
    outTexCoord = inTexCoord;
    gl_Position = cam.viewProj * worldPos;
}
