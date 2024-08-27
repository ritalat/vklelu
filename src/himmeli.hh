#pragma once

#include "memory.hh"

#include "glm/glm.hpp"
#include "vulkan/vulkan.h"

#include <memory>
#include <vector>

struct VertexInputDescription
{
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex
{
    bool operator==(const Vertex &other) const;
    static VertexInputDescription get_description();
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
};

template <>
struct std::hash<Vertex> {
    size_t operator()(const Vertex &vertex) const;
};

struct ObjFile
{
    ObjFile(const char *filename);
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct Mesh
{
    std::unique_ptr<BufferAllocation> vertexBuffer;
    std::unique_ptr<BufferAllocation> indexBuffer;
    unsigned int numVertices;
    unsigned int numIndices;
};

struct ImageFile
{
    ImageFile(const char *filename);
    ~ImageFile();
    unsigned char *pixels;
    int width;
    int height;
    int channels;
};

struct Texture {
    std::unique_ptr<ImageAllocation> image;
    VkImageView imageView;
};

struct Material
{
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkDescriptorSet textureSet = VK_NULL_HANDLE;
};

struct Himmeli
{
    Mesh *mesh;
    Material *material;
    glm::mat4 scale;
    glm::mat4 rotate;
    glm::mat4 translate;
};
