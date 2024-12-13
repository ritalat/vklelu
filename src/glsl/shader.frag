#version 460

layout (location = 0) in vec3 inFragPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoord;

layout (set = 0, binding = 1) uniform SceneData {
    vec4 cameraPos;
    vec4 lightPos;
    vec4 lightColor;
} scene;

layout (set = 2, binding = 0) uniform texture2D texture0;

layout (set = 2, binding = 1) uniform sampler s;

layout (location = 0) out vec4 outFragColor;

void main()
{
    vec3 objColor = texture(sampler2D(texture0, s), inTexCoord).rgb;
    vec3 ambient = 0.05 * objColor * scene.lightColor.rgb;

    vec3 norm = normalize(inNormal);
    vec3 lightDir = normalize(scene.lightPos.xyz - inFragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * objColor * scene.lightColor.rgb;

    vec3 camDir = normalize(scene.cameraPos.xyz - inFragPos);
    vec3 halfDir = normalize(lightDir + camDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 16.0);
    vec3 specular = spec * scene.lightColor.rgb;

    outFragColor = vec4(ambient + diffuse + specular, 1.0);
}
