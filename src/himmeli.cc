#include "himmeli.hh"

#include "utils.hh"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtx/hash.hpp"
#include "stb_image.h"
#include "tiny_obj_loader.h"
#include "vulkan/vulkan.h"

#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string_view>

bool Vertex::operator==(const Vertex &other) const {
    return position == other.position &&
           normal == other.normal &&
           texcoord == other.texcoord;
}

VertexInputDescription Vertex::get_description()
{
    VertexInputDescription description;

    VkVertexInputBindingDescription mainBinding = {};
    mainBinding.binding = 0;
    mainBinding.stride = sizeof(Vertex);
    mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    description.bindings.push_back(mainBinding);

    VkVertexInputAttributeDescription positionAttribute = {};
    positionAttribute.binding = 0;
    positionAttribute.location = 0;
    positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttribute.offset = offsetof(Vertex, position);
    description.attributes.push_back(positionAttribute);

    VkVertexInputAttributeDescription normalAttribute = {};
    normalAttribute.binding = 0;
    normalAttribute.location = 1;
    normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    normalAttribute.offset = offsetof(Vertex, normal);
    description.attributes.push_back(normalAttribute);

    VkVertexInputAttributeDescription texcoordAttribute = {};
    texcoordAttribute.binding = 0;
    texcoordAttribute.location = 2;
    texcoordAttribute.format = VK_FORMAT_R32G32_SFLOAT;
    texcoordAttribute.offset = offsetof(Vertex, texcoord);
    description.attributes.push_back(texcoordAttribute);

    return description;
}

size_t std::hash<Vertex>::operator()(const Vertex &vertex) const {
    return hash<glm::vec3>()(vertex.position) ^
           (hash<glm::vec3>()(vertex.normal) << 1) ^
           (hash<glm::vec2>()(vertex.texcoord) << 2);
}

ObjFile::ObjFile(const std::string_view filename)
{
    Path objPath = get_asset_path(filename);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, cpath(objPath), cpath(assetdir()));

    if (!warn.empty())
        fprintf(stderr, "TinyObj warn: %s\n", warn.c_str());

    if (!err.empty())
        throw std::runtime_error("TinyObj err: " + err);

    fprintf(stderr, "Model %s loaded\n", filename.data());

    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    for (const tinyobj::shape_t &shape : shapes) {
        for (const tinyobj::index_t &index : shape.mesh.indices) {
            Vertex vert;
            vert.position.x = attrib.vertices[3 * index.vertex_index + 0];
            vert.position.y = attrib.vertices[3 * index.vertex_index + 1];
            vert.position.z = attrib.vertices[3 * index.vertex_index + 2];
            vert.normal.x = attrib.normals[3 * index.normal_index + 0];
            vert.normal.y = attrib.normals[3 * index.normal_index + 1];
            vert.normal.z = attrib.normals[3 * index.normal_index + 2];
            vert.texcoord.x = attrib.texcoords[2 * index.texcoord_index + 0];
            vert.texcoord.y = 1.0f - attrib.texcoords[2 * index.texcoord_index + 1];

            if (uniqueVertices.count(vert) == 0) {
                uniqueVertices[vert] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vert);
            }
            indices.push_back(uniqueVertices[vert]);
        }
    }
}

ImageFile::ImageFile(const std::string_view filename)
{
    Path fullPath = get_asset_path(filename);

    pixels = stbi_load(cpath(fullPath), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load image: " + std::string(filename));
    }

    fprintf(stderr, "Image %s loaded\n", filename.data());
}

ImageFile::~ImageFile()
{
    if (pixels)
        stbi_image_free(pixels);
}
