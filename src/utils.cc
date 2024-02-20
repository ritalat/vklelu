#include "utils.hh"

#include "struct_helpers.hh"

#include "tiny_obj_loader.h"
#include "vulkan/vulkan.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

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

    VkVertexInputAttributeDescription colorAttribute = {};
    colorAttribute.binding = 0;
    colorAttribute.location = 2;
    colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    colorAttribute.offset = offsetof(Vertex, color);
    description.attributes.push_back(colorAttribute);

    VkVertexInputAttributeDescription uvAttribute = {};
    uvAttribute.binding = 0;
    uvAttribute.location = 3;
    uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
    uvAttribute.offset = offsetof(Vertex, uv);
    description.attributes.push_back(uvAttribute);

    return description;
}

void Mesh::load_obj_file(const char* filename, const char *baseDir)
{
    std::string objPath(filename);
    if (baseDir)
        objPath = std::string(baseDir) + objPath;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string warn;
    std::string err;

    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str(), baseDir);

    if (!warn.empty())
        fprintf(stderr, "TinyObj warn: %s\n", warn.c_str());

    if (!err.empty())
        throw std::runtime_error("TinyObj err: " + err);

    for (tinyobj::shape_t shape : shapes) {
        size_t index_offset = 0;
        size_t faces = shape.mesh.num_face_vertices.size();
        for (size_t f = 0; f < faces; ++f) {
            int faceverts = 3;
            for (size_t v = 0; v < faceverts; ++v) {
                tinyobj::index_t index = shape.mesh.indices[index_offset + v];
                tinyobj::real_t vx = attrib.vertices[3 * index.vertex_index + 0];
                tinyobj::real_t vy = attrib.vertices[3 * index.vertex_index + 1];
                tinyobj::real_t vz = attrib.vertices[3 * index.vertex_index + 2];
                tinyobj::real_t nx = attrib.normals[3 * index.normal_index + 0];
                tinyobj::real_t ny = attrib.normals[3 * index.normal_index + 1];
                tinyobj::real_t nz = attrib.normals[3 * index.normal_index + 2];
                tinyobj::real_t ux = attrib.texcoords[2 * index.texcoord_index + 0];
                tinyobj::real_t uy = attrib.texcoords[2 * index.texcoord_index + 1];

                Vertex vert;
                vert.position.x = vx;
                vert.position.y = vy;
                vert.position.z = vz;
                vert.normal.x = nx;
                vert.normal.y = ny;
                vert.normal.z = nz;
                vert.color = vert.normal;
                vert.uv.x = ux;
                vert.uv.y = 1-uy;

                vertices.push_back(vert);
            }
            index_offset += faceverts;
        }
    }
}

void PipelineBuilder::use_default_ff()
{
    inputAssembly = input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    rasterizer = rasterization_state_create_info(VK_POLYGON_MODE_FILL);
    multisampling = multisampling_state_create_info();
    colorBlendAttachment = color_blend_attachment_state();
    depthStencil = depth_stencil_create_info(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass)
{
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = nullptr;
    pipelineInfo.stageCount = shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.renderPass = pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline newPipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create pipeline\n");
        return VK_NULL_HANDLE;
    } else {
        return newPipeline;
    }
}
