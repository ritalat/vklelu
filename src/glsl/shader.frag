#version 460

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec3 inFragPos;

layout (location = 0) out vec4 outFragColor;

layout (set = 0, binding = 1) uniform SceneData {
    vec4 cameraPos;
    vec4 lightPos;
    vec4 lightColor;
} scene;

layout (set = 2, binding = 0) uniform sampler2D tex1;

float ambientStrenght = 0.05f;
float specularStrenght = 0.5f;
float shininess = 32.0f;

void main()
{
    vec3 ambient = ambientStrenght * scene.lightColor.rgb;

    vec3 norm = normalize(inNormal);
    vec3 lightDir = normalize(scene.lightPos.xyz - inFragPos);

    float diff = max(dot(norm, lightDir), 0.0f);
    vec3 diffuse = diff * scene.lightColor.rgb;

    vec3 specular = vec3(0.0f);
    if (diff > 0.0f) {
        vec3 camDir = normalize(scene.cameraPos.xyz - inFragPos);
        vec3 reflectDir = reflect(-lightDir, norm);

        float spec = pow(max(dot(camDir, reflectDir), 0.0f), shininess);
        specular = specularStrenght * spec * scene.lightColor.rgb;
    }

    vec3 objColor = texture(tex1, inTexCoord).xyz;
    outFragColor =  vec4((ambient + diffuse + specular) * objColor, 1.0f);
}
