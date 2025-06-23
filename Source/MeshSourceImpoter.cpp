#include "HydraEngine/Base.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

import Assets;
import HE;
import Math;
import std;

#define HE_PROFILE_COLOR 0xAA0000
#define HE_PROFILE_MAIN_THREAD 0xAAAA00

namespace Assets {

    struct IndexRange
    {
        size_t from = 0;
        size_t to = 0;
    };

    MeshSourceImporter::MeshSourceImporter(AssetManager* pAssetManager)
        : assetManager(pAssetManager)
    {
    }

    static Math::float4x4 GetNodeTransform(cgltf_node* node)
    {
        if (node->has_matrix)
        {
            return Math::make_mat4(node->matrix);
        }
        else
        {
            Math::float4x4 translation = node->has_translation ? Math::translate(Math::float4x4(1.0f), Math::float3(node->translation[0], node->translation[1], node->translation[2])) : Math::float4x4(1);
            Math::float4x4 rotation = node->has_rotation ? Math::toMat4(Math::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2])) : Math::float4x4(1);
            Math::float4x4 scale = node->has_scale ? Math::scale(Math::float4x4(1.0f), Math::float3(node->scale[0], node->scale[1], node->scale[2])) : Math::float4x4(1);

            return translation * rotation * scale;
        }
    }

    static void CalculateTangentBitangent(
        const Math::float3& v0, const Math::float3& v1, const Math::float3& v2,
        const Math::float2& uv0, const Math::float2& uv1, const Math::float2& uv2,
        const Math::float3& n0, const Math::float3& n1, const Math::float3& n2,
        Math::float3& tangent0, Math::float3& tangent1, Math::float3& tangent2,
        Math::float3& bitangent0, Math::float3& bitangent1, Math::float3& bitangent2
    )
    {
        // Edge vectors of the triangle in model space
        Math::float3 edge1 = v1 - v0;
        Math::float3 edge2 = v2 - v0;

        // UV differences
        Math::float2 deltaUV1 = uv1 - uv0;
        Math::float2 deltaUV2 = uv2 - uv0;

        // Compute the determinant (area of the UV triangle)
        float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

        // Compute the tangent and bitangent vectors for the triangle
        tangent0.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent0.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent0.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

        bitangent0.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent0.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent0.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

        tangent0 = Math::normalize(tangent0);
        bitangent0 = Math::normalize(bitangent0);

        tangent0 = Math::normalize(tangent0 - Math::dot(tangent0, n0) * n0);
        bitangent0 = Math::normalize(bitangent0 - Math::dot(bitangent0, n0) * n0);

        tangent1 = Math::normalize(tangent0 - Math::dot(tangent0, n1) * n1);
        bitangent1 = Math::normalize(bitangent0 - Math::dot(bitangent0, n1) * n1);

        tangent2 = Math::normalize(tangent0 - Math::dot(tangent0, n2) * n2);
        bitangent2 = Math::normalize(bitangent0 - Math::dot(bitangent0, n2) * n2);
    }

    static std::pair<const uint8_t*, size_t> BufferIterator(const cgltf_accessor* accessor, size_t defaultStride)
    {
        const cgltf_buffer_view* view = accessor->buffer_view;
        const uint8_t* data = (uint8_t*)view->buffer->data + view->offset + accessor->offset;
        const size_t stride = view->stride ? view->stride : defaultStride;
        return std::make_pair(data, stride);
    }

    static const char* CgltfErrorToString(cgltf_result res)
    {
        switch (res)
        {
        case cgltf_result_success:         return "Success";
        case cgltf_result_data_too_short:  return "Data is too short";
        case cgltf_result_unknown_format:  return "Unknown format";
        case cgltf_result_invalid_json:    return "Invalid JSON";
        case cgltf_result_invalid_gltf:    return "Invalid glTF";
        case cgltf_result_invalid_options: return "Invalid options";
        case cgltf_result_file_not_found:  return "File not found";
        case cgltf_result_io_error:        return "I/O error";
        case cgltf_result_out_of_memory:   return "Out of memory";
        case cgltf_result_legacy_gltf:     return "Legacy glTF";
        default:                           return "Unknown error";
        }
    }

    static void AppendNodes(MeshSource* meshSource, Node& node, cgltf_node* cgltfNode, const cgltf_data* data, const std::unordered_map<const cgltf_mesh*, Mesh*>& meshMap)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        if (cgltfNode->mesh)
        {
            if (meshMap.contains(cgltfNode->mesh))
            {
                const auto& mesh = *meshMap.at(cgltfNode->mesh);
                node.meshIndex = mesh.globalMeshIndex;
            }
        }

        node.meshSource = meshSource;
        node.childrenOffset = (uint32_t)meshSource->nodes.size();
        node.childrenCount = (uint32_t)cgltfNode->children_count;

        for (cgltf_size i = 0; i < cgltfNode->children_count; i++)
        {
            cgltf_node* child = cgltfNode->children[i];
            Node& newNode = meshSource->nodes.emplace_back();
            newNode.name = child->name ? child->name : "None";
            newNode.transform = GetNodeTransform(child);
        }

        for (cgltf_size i = 0; i < cgltfNode->children_count; i++)
        {
            Node& newNode = meshSource->nodes[node.childrenOffset + i];
            cgltf_node* child = cgltfNode->children[i];

            AppendNodes(meshSource, newNode, child, data, meshMap);
        }
    }

    void ImportTexture(AssetManager* assetManager, Texture* textureAsset, HE::Buffer buffer, nvrhi::IDevice* device, const std::string& name)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        assetManager->asyncTaskCount++;

        HE::Image image(buffer);
        uint8_t* data = image.ExtractData();

        nvrhi::TextureDesc desc;
        desc.width = image.GetWidth();
        desc.height = image.GetHeight();
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = name;

        nvrhi::TextureHandle texture = device->createTexture(desc);
        textureAsset->texture = texture;

        HE::Jops::SubmitToMainThread([assetManager, device, textureAsset, desc, data, name]() {

            HE_PROFILE_SCOPE_NC("ImportTexture::SubmitToMainThread", HE_PROFILE_MAIN_THREAD);

            assetManager->AddMemoryOnlyAsset(textureAsset);

            auto commandList = device->createCommandList({ .enableImmediateExecution = false });

            commandList->open();
            commandList->beginTrackingTextureState(textureAsset->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
            commandList->writeTexture(textureAsset->texture, 0, 0, data, desc.width * 4);
            commandList->setPermanentTextureState(textureAsset->texture, nvrhi::ResourceStates::ShaderResource);
            commandList->commitBarriers();
            commandList->close();
            device->executeCommandList(commandList);
            device->runGarbageCollection();
            commandList.Reset();

            std::free(data);
            textureAsset->state = AssetState::Loaded;
            assetManager->OnAssetLoaded(textureAsset);

            assetManager->asyncTaskCount--;
        });
    }

    void AppendMeshes(cgltf_data* data, MeshSource* meshSource, std::unordered_map<const cgltf_mesh*, Mesh*>& meshMap, const std::unordered_map<const cgltf_material*, Material*>& materials)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        bool c_ForceRebuildTangents = true;
        size_t totalIndices = 0;
        size_t totalVertices = 0;
        bool hasJoints = false;
        bool hasUV1 = false;
        uint32_t geometryCount = 0;

        for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; mesh_idx++)
        {
            const cgltf_mesh& mesh = data->meshes[mesh_idx];

            for (size_t prim_idx = 0; prim_idx < mesh.primitives_count; prim_idx++)
            {
                const cgltf_primitive& prim = mesh.primitives[prim_idx];
                
                geometryCount++;

                if ((prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_line_strip && prim.type != cgltf_primitive_type_lines) || prim.attributes_count == 0)
                    continue;

                if (prim.indices)
                    totalIndices += prim.indices->count;
                else
                    totalIndices += prim.attributes->data->count;
                totalVertices += prim.attributes->data->count;

                if (!hasUV1)
                {
                    for (size_t attr_idx = 0; attr_idx < prim.attributes_count; attr_idx++)
                    {
                        const cgltf_attribute& attr = prim.attributes[attr_idx];
                        if (attr.type == cgltf_attribute_type_texcoord)
                        {
                            if (attr.index == 1)
                            {
                                hasUV1 = true;
                                break;
                            }
                        }
                    }
                }

                if (!hasJoints)
                {
                    for (size_t attr_idx = 0; attr_idx < prim.attributes_count; attr_idx++)
                    {
                        const cgltf_attribute& attr = prim.attributes[attr_idx];
                        if (attr.type == cgltf_attribute_type_joints || attr.type == cgltf_attribute_type_weights)
                        {
                            hasJoints = true;
                            break;
                        }
                    }
                }
            }
        }

        meshSource->cpuIndexBuffer.resize(totalIndices);

        uint32_t positionByteSize = uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::Position));
        uint32_t normalByteSize = uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::Normal));
        uint32_t tangentByteSize = uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::Tangent));
        uint32_t texCoordByteSize = uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::TexCoord0));
        uint32_t boneIndicesByteSize = uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::BoneIndices));
        uint32_t boneWeightByteSize = uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::BoneWeights));

        uint32_t bufferSize = 0;
        bufferSize += positionByteSize;
        bufferSize += normalByteSize;
        bufferSize += tangentByteSize;
        bufferSize += texCoordByteSize;

        if (hasUV1)
        {
            bufferSize += texCoordByteSize;
        }

        if (hasJoints)
        {
            bufferSize += uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::BoneIndices));
            bufferSize += uint32_t(totalVertices * GetVertexAttributeSize(VertexAttribute::BoneWeights));
        }

        meshSource->cpuVertexBuffer.resize(bufferSize);
        meshSource->vertexCount = (uint32_t)totalVertices;

        meshSource->vertexBufferRanges[int(VertexAttribute::Position)]  = { 0                                                   , positionByteSize };
        meshSource->vertexBufferRanges[int(VertexAttribute::Normal)]    = { positionByteSize                                    , normalByteSize   };
        meshSource->vertexBufferRanges[int(VertexAttribute::Tangent)]   = { positionByteSize + normalByteSize                   , tangentByteSize  };
        meshSource->vertexBufferRanges[int(VertexAttribute::TexCoord0)] = { positionByteSize + normalByteSize + tangentByteSize , texCoordByteSize };

        if (hasUV1)
        {
            meshSource->vertexBufferRanges[int(VertexAttribute::TexCoord1)] = { positionByteSize + normalByteSize + tangentByteSize + texCoordByteSize , texCoordByteSize };
        }

        totalIndices = 0;
        totalVertices = 0;

        std::vector<Math::float3> computedTangents;
        std::vector<Math::float3> computedBitangents;

        meshSource->meshes.reserve(data->meshes_count);
        meshSource->geometries.reserve(geometryCount);

        geometryCount = 0;

        for (size_t mesh_idx = 0; mesh_idx < data->meshes_count; mesh_idx++)
        {
            const cgltf_mesh& cltfMesh = data->meshes[mesh_idx];

            Mesh& mesh = meshSource->meshes.emplace_back();
            meshMap[&cltfMesh] = &mesh;

            if (cltfMesh.name)
            {
                mesh.name = cltfMesh.name;
            }
            mesh.meshSource = meshSource;
            mesh.indexOffset = (uint32_t)totalIndices;
            mesh.vertexOffset = (uint32_t)totalVertices;
           
            mesh.geometryOffset = geometryCount;
            mesh.geometryCount = (uint32_t)cltfMesh.primitives_count;

            geometryCount += mesh.geometryCount;

            for (size_t prim_idx = 0; prim_idx < cltfMesh.primitives_count; prim_idx++)
            {
                const cgltf_primitive& prim = cltfMesh.primitives[prim_idx];

                if ((prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_line_strip && prim.type != cgltf_primitive_type_lines) || prim.attributes_count == 0)
                    continue;

                if (prim.type == cgltf_primitive_type_line_strip || prim.type == cgltf_primitive_type_lines)
                    mesh.type = MeshType::CurvePolytubes;

                if (prim.indices)
                {
                    HE_ASSERT(
                        prim.indices->component_type == cgltf_component_type_r_32u ||
                        prim.indices->component_type == cgltf_component_type_r_16u ||
                        prim.indices->component_type == cgltf_component_type_r_8u
                    );
                    HE_ASSERT(prim.indices->type == cgltf_type_scalar);
                }

                const cgltf_accessor* positionsAccessor = nullptr;
                const cgltf_accessor* normalsAccessor = nullptr;
                const cgltf_accessor* tangentsAccessor = nullptr;
                const cgltf_accessor* texcoords0Accessor = nullptr;
                const cgltf_accessor* texcoords1Accessor = nullptr;
                const cgltf_accessor* joint_weightsAccessor = nullptr;
                const cgltf_accessor* joint_indicesAccessor = nullptr;

                for (size_t attr_idx = 0; attr_idx < prim.attributes_count; attr_idx++)
                {
                    const cgltf_attribute& attr = prim.attributes[attr_idx];

                    switch (attr.type)
                    {
                    case cgltf_attribute_type_position:
                        HE_ASSERT(attr.data->type == cgltf_type_vec3);
                        HE_ASSERT(attr.data->component_type == cgltf_component_type_r_32f);
                        positionsAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_normal:
                        HE_ASSERT(attr.data->type == cgltf_type_vec3);
                        HE_ASSERT(attr.data->component_type == cgltf_component_type_r_32f);
                        normalsAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_tangent:
                        HE_ASSERT(attr.data->type == cgltf_type_vec4);
                        HE_ASSERT(attr.data->component_type == cgltf_component_type_r_32f);
                        tangentsAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_texcoord:
                        HE_ASSERT(attr.data->type == cgltf_type_vec2);
                        HE_ASSERT(attr.data->component_type == cgltf_component_type_r_32f);
                        if (attr.index == 0)
                            texcoords0Accessor = attr.data;
                        if (attr.index == 1)
                            texcoords1Accessor = attr.data;
                        break;
                    case cgltf_attribute_type_joints:
                        HE_ASSERT(attr.data->type == cgltf_type_vec4);
                        HE_ASSERT(attr.data->component_type == cgltf_component_type_r_8u || attr.data->component_type == cgltf_component_type_r_16u);
                        joint_indicesAccessor = attr.data;
                        break;
                    case cgltf_attribute_type_weights:
                        HE_ASSERT(attr.data->type == cgltf_type_vec4);
                        HE_ASSERT(attr.data->component_type == cgltf_component_type_r_8u || attr.data->component_type == cgltf_component_type_r_16u || attr.data->component_type == cgltf_component_type_r_32f);
                        joint_weightsAccessor = attr.data;
                        break;
                    default:
                        break;
                    }
                }

                HE_ASSERT(positionsAccessor);

                size_t indexCount = 0;

                if (prim.indices)
                {
                    indexCount = prim.indices->count;

                    auto [indexSrc, indexStride] = BufferIterator(prim.indices, 0);
                    uint32_t* indexDst = meshSource->cpuIndexBuffer.data() + totalIndices;

                    switch (prim.indices->component_type)
                    {
                    case cgltf_component_type_r_8u:
                        if (!indexStride) indexStride = sizeof(uint8_t);
                        for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                        {
                            *indexDst = *(const uint8_t*)indexSrc;

                            indexSrc += indexStride;
                            indexDst++;
                        }
                        break;
                    case cgltf_component_type_r_16u:
                        if (!indexStride) indexStride = sizeof(uint16_t);
                        for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                        {
                            *indexDst = *(const uint16_t*)indexSrc;

                            indexSrc += indexStride;
                            indexDst++;
                        }
                        break;
                    case cgltf_component_type_r_32u:
                        if (!indexStride) indexStride = sizeof(uint32_t);
                        for (size_t i_idx = 0; i_idx < indexCount; i_idx++)
                        {
                            *indexDst = *(const uint32_t*)indexSrc;

                            indexSrc += indexStride;
                            indexDst++;
                        }
                        break;
                    default:
                        HE_ASSERT(false);
                    }
                }

                Math::box3 bounds = Math::box3::empty();

                if (positionsAccessor)
                {
                    Math::float3* positionDst = meshSource->GetAttribute<Math::float3>(VertexAttribute::Position) + totalVertices;

                    for (size_t v_idx = 0; v_idx < positionsAccessor->count; v_idx++)
                    {
                        cgltf_float pos[3];
                        cgltf_accessor_read_float(positionsAccessor, v_idx, pos, 3);
                        *positionDst = Math::float3(pos[0], pos[1], pos[2]);
                        bounds |= *positionDst;

                        ++positionDst;
                    }
                }

                if (normalsAccessor)
                {
                    HE_ASSERT(normalsAccessor->count == positionsAccessor->count);
                    uint32_t* normalDst = meshSource->GetAttribute<uint32_t>(VertexAttribute::Normal) + totalVertices;

                    for (size_t v_idx = 0; v_idx < normalsAccessor->count; v_idx++)
                    {
                        cgltf_float norm[3];
                        cgltf_accessor_read_float(normalsAccessor, v_idx, norm, 3);
                        *normalDst = Math::vectorToSnorm8(Math::float3(norm[0], norm[1], norm[2]));
                        ++normalDst;
                    }
                }

                if (tangentsAccessor)
                {
                    HE_ASSERT(tangentsAccessor->count == positionsAccessor->count);
                    uint32_t* tangentDst = meshSource->GetAttribute<uint32_t>(VertexAttribute::Tangent) + totalVertices;
                    for (size_t v_idx = 0; v_idx < tangentsAccessor->count; v_idx++)
                    {
                        cgltf_float tang[4];
                        cgltf_accessor_read_float(tangentsAccessor, v_idx, tang, 4);
                        *tangentDst = Math::vectorToSnorm8(Math::float4(tang[0], tang[1], tang[2], tang[3]));
                        ++tangentDst;
                    }
                }

                if (texcoords0Accessor)
                {
                    HE_ASSERT(texcoords0Accessor->count == positionsAccessor->count);
                    Math::float2* texcoordDst = meshSource->GetAttribute<Math::float2>(VertexAttribute::TexCoord0) + totalVertices;

                    for (size_t v_idx = 0; v_idx < texcoords0Accessor->count; v_idx++)
                    {
                        cgltf_float texcoord[2];
                        cgltf_accessor_read_float(texcoords0Accessor, v_idx, texcoord, 2);
                        *texcoordDst = Math::float2(texcoord[0], texcoord[1]);
                        ++texcoordDst;
                    }
                }
                else
                {
                    Math::float2* texcoordDst = meshSource->GetAttribute<Math::float2>(VertexAttribute::TexCoord0) + totalVertices;
                    for (size_t v_idx = 0; v_idx < positionsAccessor->count; v_idx++)
                    {
                        *texcoordDst = Math::float2(0.f);
                        ++texcoordDst;
                    }
                }

                if (texcoords1Accessor)
                {
                    HE_ASSERT(texcoords0Accessor->count == positionsAccessor->count);
                    Math::float2* texcoordDst = meshSource->GetAttribute<Math::float2>(VertexAttribute::TexCoord1) + totalVertices;

                    for (size_t v_idx = 0; v_idx < texcoords1Accessor->count; v_idx++)
                    {
                        cgltf_float texcoord[2];
                        cgltf_accessor_read_float(texcoords1Accessor, v_idx, texcoord, 2);
                        *texcoordDst = Math::float2(texcoord[0], texcoord[1]);
                        ++texcoordDst;
                    }
                }

                if (normalsAccessor && texcoords0Accessor && (!tangentsAccessor || c_ForceRebuildTangents))
                {
                    computedTangents.resize(positionsAccessor->count);
                    computedBitangents.resize(positionsAccessor->count);

                    for (cgltf_size i = 0; i < prim.indices->count; i += 3)
                    {
                        // Get the indices of the triangle vertices
                        uint32_t i0 = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
                        uint32_t i1 = (uint32_t)cgltf_accessor_read_index(prim.indices, i + 1);
                        uint32_t i2 = (uint32_t)cgltf_accessor_read_index(prim.indices, i + 2);

                        // Read positions
                        Math::float3 p0, p1, p2;
                        cgltf_accessor_read_float(positionsAccessor, i0, Math::value_ptr(p0), 3);
                        cgltf_accessor_read_float(positionsAccessor, i1, Math::value_ptr(p1), 3);
                        cgltf_accessor_read_float(positionsAccessor, i2, Math::value_ptr(p2), 3);

                        // Read UVs
                        Math::float2 uv0, uv1, uv2;
                        cgltf_accessor_read_float(texcoords0Accessor, i0, Math::value_ptr(uv0), 2);
                        cgltf_accessor_read_float(texcoords0Accessor, i1, Math::value_ptr(uv1), 2);
                        cgltf_accessor_read_float(texcoords0Accessor, i2, Math::value_ptr(uv2), 2);

                        // Read normals
                        Math::float3 n0, n1, n2;
                        cgltf_accessor_read_float(normalsAccessor, i0, Math::value_ptr(n0), 3);
                        cgltf_accessor_read_float(normalsAccessor, i1, Math::value_ptr(n1), 3);
                        cgltf_accessor_read_float(normalsAccessor, i2, Math::value_ptr(n2), 3);

                        // Calculate tangent and bitangent
                        Math::float3 tangent0, tangent1, tangent2;
                        Math::float3 bitangent0, bitangent1, bitangent2;

                        CalculateTangentBitangent(
                            p0, p1, p2,
                            uv0, uv1, uv2,
                            n0, n1, n2,
                            tangent0, tangent1, tangent2,
                            bitangent0, bitangent1, bitangent2
                        );

                        computedTangents[i0] = tangent0;
                        computedTangents[i1] = tangent1;
                        computedTangents[i2] = tangent2;

                        computedBitangents[i0] = bitangent0;
                        computedBitangents[i1] = bitangent1;
                        computedBitangents[i2] = bitangent2;
                    }

                    uint32_t* tangentDst = meshSource->GetAttribute<uint32_t>(VertexAttribute::Tangent) + totalVertices;

                    for (size_t v_idx = 0; v_idx < positionsAccessor->count; v_idx++)
                    {
                        Math::float3 normal;
                        cgltf_accessor_read_float(normalsAccessor, v_idx, &normal.x, 3);

                        Math::float3 tangent = computedTangents[v_idx];
                        Math::float3 bitangent = computedBitangents[v_idx];

                        float sign = 0;
                        float tangentLength = Math::length(tangent);
                        float bitangentLength = Math::length(bitangent);
                        if (tangentLength > 0 && bitangentLength > 0)
                        {
                            tangent /= tangentLength;
                            bitangent /= bitangentLength;
                            Math::float3 cross_b = Math::cross(normal, tangent);
                            sign = (Math::dot(cross_b, bitangent) > 0) ? -1.f : 1.f;
                        }

                        *tangentDst = Math::vectorToSnorm8(Math::float4(tangent, sign));
                        ++tangentDst;
                    }
                }

                MeshGeometry& geometry = meshSource->geometries.emplace_back();

                if (materials.contains(prim.material))
                {
                    geometry.materailHandle = materials.at(prim.material)->handle;
                }

                geometry.mesh = &mesh;
                geometry.indexOffsetInMesh = mesh.indexCount;
                geometry.vertexOffsetInMesh = mesh.vertexCount;
                geometry.indexCount = (uint32_t)indexCount;
                geometry.vertexCount = (uint32_t)positionsAccessor->count;
                geometry.aabb = bounds;
                switch (prim.type)
                {
                case cgltf_primitive_type_triangles:  geometry.type = MeshGeometryPrimitiveType::Triangles;  break;
                case cgltf_primitive_type_lines:      geometry.type = MeshGeometryPrimitiveType::Lines;      break;
                case cgltf_primitive_type_line_strip: geometry.type = MeshGeometryPrimitiveType::LineStrip;  break;
                }

                mesh.aabb |= bounds;
                mesh.indexCount += geometry.indexCount;
                mesh.vertexCount += geometry.vertexCount;

                totalIndices += geometry.indexCount;
                totalVertices += geometry.vertexCount;
            }
        }

        {
            int meshIndex = 0;
            int geometryIndex = 0;
            for (auto& mesh : meshSource->meshes)
            {
                for (auto& geometry : meshSource->geometries)
                {
                    geometry.globalGeometryIndex = geometryIndex;
                    ++geometryIndex;
                }

                mesh.globalMeshIndex = meshIndex;
                ++meshIndex;
            }
        }
    }

    cgltf_data* LoadGltfData(cgltf_options options, const char* cStrFilePath)
    {
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, cStrFilePath, &data);
        if (result != cgltf_result_success)
        {
            HE_ERROR("{}", CgltfErrorToString(result));
            return nullptr;
        }
    
        result = cgltf_load_buffers(&options, data, cStrFilePath);
        if (result != cgltf_result_success)
        {
            HE_ERROR("{}", CgltfErrorToString(result));
            cgltf_free(data);
            return nullptr;
        }
    
        result = cgltf_validate(data);
        if (result != cgltf_result_success)
        {
            HE_ERROR("{}", CgltfErrorToString(result));
            cgltf_free(data);
            return nullptr;
        }

        return data;
    }

    void AppendMaterials(
        AssetManager* assetManager,
        cgltf_data* data ,
        std::unordered_map<const cgltf_material*, Material*>& materials,
        const AssetMetadata& metadata,
        IndexRange materialsIndexRange,
        MeshSource* meshSource
    )
    {
        HE::Timer t;
        for (cgltf_size i = 0; i < data->materials_count; i++)
        {
            const cgltf_material& cgltfMat = data->materials[i];

            auto material = &assetManager->materials[materialsIndexRange.from + i];
            materials[&cgltfMat] = material;

            material->name = cgltfMat.name ? cgltfMat.name : "Unnamed Material";
            HE_INFO("Import Memory Only material [{}][{}]", metadata.filePath.string(), material->name);

            if (cgltfMat.has_pbr_metallic_roughness)
            {
                const cgltf_float* base_color = cgltfMat.pbr_metallic_roughness.base_color_factor;
                material->baseColor = { base_color[0], base_color[1], base_color[2], base_color[3] };

                cgltf_int index = cgltfMat.pbr_metallic_roughness.base_color_texture.texcoord;
                material->uvSet = index == 0 ? UVSet::UV0 : UVSet::UV1;

                if (cgltfMat.pbr_metallic_roughness.base_color_texture.texture)
                {
                    uint32_t index = uint32_t(cgltfMat.pbr_metallic_roughness.base_color_texture.texture - data->textures);
                    material->baseTextureHandle = meshSource->GetTextureSpan()[index];
                }
            }

            if (cgltfMat.has_pbr_specular_glossiness)
            {
                const cgltf_float* base_color = cgltfMat.pbr_specular_glossiness.diffuse_factor;
                material->baseColor = { base_color[0], base_color[1], base_color[2], base_color[2] };

                cgltf_int index = cgltfMat.pbr_specular_glossiness.diffuse_texture.texcoord;
                material->uvSet = index == 0 ? UVSet::UV0 : UVSet::UV1;

                if (cgltfMat.pbr_specular_glossiness.diffuse_texture.texture)
                {
                    uint32_t index = uint32_t(cgltfMat.pbr_specular_glossiness.diffuse_texture.texture - data->textures);
                    material->baseTextureHandle = meshSource->GetTextureSpan()[index];
                }
            }

            meshSource->dependencies[i] = material->handle;
            material->state = AssetState::Loaded;
            assetManager->AddMemoryOnlyAsset(material);
            assetManager->OnAssetLoaded(material);
        }


        HE_INFO("Import Memory Only materials [{}][{}][{}ms]", metadata.filePath.string(), data->materials_count, t.ElapsedMilliseconds());
    }

    void AppendNodes(
        MeshSource* meshSource,
        cgltf_data* data, 
        const AssetMetadata& metadata,
        std::unordered_map<const cgltf_mesh*, Mesh*>& meshMap
    )
    {
        HE::Timer t;

        meshSource->nodes.reserve(data->nodes_count);
        meshSource->root.name = data->scene->name ? data->scene->name : "Model";
        meshSource->root.transform = Math::float4x4(1.0f);
        meshSource->root.childrenOffset = 0;
        meshSource->root.childrenCount = (uint32_t)data->scene->nodes_count;
        meshSource->root.meshSource = meshSource;

        for (cgltf_size i = 0; i < data->scene->nodes_count; i++)
        {
            cgltf_node* cgltfNode = data->scene->nodes[i];
            Node& node = meshSource->nodes.emplace_back();
            node.name = cgltfNode->name ? cgltfNode->name : "Node";
            node.transform = GetNodeTransform(cgltfNode);
        }

        for (cgltf_size i = 0; i < data->scene->nodes_count; i++)
        {
            cgltf_node* cgltfNode = data->scene->nodes[i];
            Node& node = meshSource->nodes[meshSource->root.childrenOffset + i];

            AppendNodes(meshSource, node, cgltfNode, data, meshMap);
        }

        HE_INFO("Import AppendNodes [{}][{}ms]", metadata.filePath.string(), t.ElapsedMilliseconds());
    }

    Asset* MeshSourceImporter::Import(const AssetMetadata& metadata)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        HE::Timer t;

        auto path = assetManager->desc.assetsDirectory / metadata.filePath;
        auto filePath = path.lexically_normal().string();
        auto cStrFilePath = filePath.c_str();

        if (!std::filesystem::exists(path))
        {
            HE_ERROR("MeshSourceImporter : file {} not exists", cStrFilePath);
            return nullptr;
        }
        
        cgltf_options options = {};
        cgltf_data* data = LoadGltfData(options, cStrFilePath);
        if (!data)
        {
            return nullptr;
        }

        auto meshSource = &assetManager->meshSources.emplace_back();
        meshSource->state = AssetState::Loading;

        meshSource->dependencies.resize(data->materials_count + data->textures_count);
        meshSource->materialCount = (uint32_t)data->materials_count;
        meshSource->textureCount = (uint32_t)data->textures_count;
        memset(meshSource->dependencies.data(), 0, meshSource->dependencies.size());

        IndexRange materialsIndexRange = { assetManager->materials.size() , assetManager->materials.size() + meshSource->materialCount };
        IndexRange textureIndexRange = { assetManager->textures.size() , assetManager->textures.size() + meshSource->textureCount };

        assetManager->ReserveRange(AssetType::Material, materialsIndexRange.to);
        assetManager->ReserveRange(AssetType::Texture2D, textureIndexRange.to);

        std::unordered_map<const cgltf_material*, Material*> materials;
        std::unordered_map<const cgltf_mesh*, Mesh*> meshMap;

        //  Textures 
        {
            HE::Timer t;

            for (cgltf_size i = 0; i < data->textures_count; i++)
            {
                const cgltf_texture* cgltfTexture = &data->textures[i];
                const cgltf_image* image = cgltfTexture->image;
                std::string name = image->name ? image->name : "Unnamed";
                HE_INFO("Import Memory Only texture : [{}][{}]", metadata.filePath.string(), name);

                uint8_t* dataPtr = static_cast<uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
                const size_t dataSize = image->buffer_view->size;

                auto textureAsset = &assetManager->textures[textureIndexRange.from + i];

                ImportTexture(assetManager, textureAsset, HE::Buffer{ dataPtr ,dataSize }, assetManager->device, name);
                meshSource->dependencies[meshSource->materialCount + i] = textureAsset->handle;
            }

            HE_INFO("Import Memory Only textures [{}][{}][{}ms]", metadata.filePath.string(), data->textures_count, t.ElapsedMilliseconds());
        }

        AppendMaterials(assetManager, data, materials, metadata, materialsIndexRange, meshSource);
        AppendMeshes(data, meshSource, meshMap, materials);
        AppendNodes(meshSource, data, metadata, meshMap);

        cgltf_free(data);
        meshSource->state = AssetState::Loaded;

        return meshSource;
    }

    Asset* MeshSourceImporter::ImportAsync(const AssetMetadata& metadata)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        HE::Timer t;

        auto path = assetManager->desc.assetsDirectory / metadata.filePath;

        if (!std::filesystem::exists(path))
        {
            HE_ERROR("MeshSourceImporter : file {} not exists", path.string());
            return nullptr;
        }

        auto meshSource = &assetManager->meshSources.emplace_back();
        meshSource->state = AssetState::Loading;

        HE::Jops::SubmitTask([this, meshSource, metadata, path]() {

            HE_PROFILE_SCOPE_NC("ImportAsync::SubmitTask", HE_PROFILE_COLOR);

            auto filePath = path.lexically_normal().string();
            auto cStrFilePath = filePath.c_str();

            cgltf_options options = {};
            cgltf_data* data = LoadGltfData(options, cStrFilePath);

            meshSource->dependencies.resize(data->materials_count + data->textures_count);
            meshSource->materialCount = (uint32_t)data->materials_count;
            meshSource->textureCount = (uint32_t)data->textures_count;
            memset(meshSource->dependencies.data(), 0, meshSource->dependencies.size());

            IndexRange materialsIndexRange = { assetManager->materials.size() , assetManager->materials.size() + meshSource->materialCount };
            IndexRange textureIndexRange = { assetManager->textures.size() , assetManager->textures.size() + meshSource->textureCount };

            assetManager->ReserveRange(AssetType::Material, materialsIndexRange.to);
            assetManager->ReserveRange(AssetType::Texture2D, textureIndexRange.to);

            HE::Jops::Taskflow tf;

            std::unordered_map<const cgltf_material*, Material*> materials;
            std::unordered_map<const cgltf_mesh*, Mesh*> meshMap;

            std::vector<tf::Task> textureTasks;
            textureTasks.reserve(data->textures_count);
  
            // Textures
            {
                HE::Timer t;

                for (cgltf_size i = 0; i < data->textures_count; i++)
                {
                    const cgltf_texture* cgltfTexture = &data->textures[i];
                    const cgltf_image* image = cgltfTexture->image;
                    std::string name = image->name ? image->name : "Unnamed";
                    HE_INFO("Import memory only texture : [{}][{}]", metadata.filePath.string(), name);

                    uint8_t* dataPtr = static_cast<uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
                    const size_t dataSize = image->buffer_view->size;

                    auto textureAsset = &assetManager->textures[textureIndexRange.from + i];

                    auto task = tf.emplace([this, textureAsset, dataPtr, dataSize, name]() { ImportTexture(assetManager, textureAsset, HE::Buffer{ dataPtr ,dataSize }, assetManager->device, name); });
                    textureTasks.emplace_back(task);

                    meshSource->dependencies[meshSource->materialCount + i] = textureAsset->handle;
                }

                HE_INFO("Import memory only textures [{}][{}][{}ms]", metadata.filePath.string(), data->textures_count, t.ElapsedMilliseconds());
            }

            AppendMaterials(assetManager, data, materials, metadata, materialsIndexRange, meshSource);
            AppendMeshes(data, meshSource, meshMap, materials);
            AppendNodes(meshSource, data, metadata, meshMap);

            auto finalTask = tf.emplace([this, &data, meshSource]() {

                cgltf_free(data);
                assetManager->OnAssetLoaded(meshSource);
            });

            for (auto& t : textureTasks)
                t.precede(finalTask);

            meshSource->state = AssetState::Loaded;
            HE::Jops::RunTaskflow(tf).wait();
        });

        HE_ERROR("[Import meshSource] [{}][{}ms]", metadata.filePath.string(), t.ElapsedMilliseconds());

        return meshSource;
    }

    void MeshSourceImporter::Save(Asset* asset, const AssetMetadata& metadata)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        NOT_YET_IMPLEMENTED();
    }

    Asset* MeshSourceImporter::Create(const std::filesystem::path& filePath)
    {
        HE_PROFILE_SCOPE_COLOR(HE_PROFILE_COLOR);

        NOT_YET_IMPLEMENTED();
        return nullptr;
    }
}