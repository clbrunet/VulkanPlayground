#include "voxelizer.hpp"
#include "math.hpp"
#include "filesystem.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <assimp/scene.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <iostream>
#include <span>


static void dda(glm::vec3 const& a, glm::vec3 const& b, std::invocable<glm::ivec3 const&> auto const& fn) {
    auto direction = glm::normalize(b - a);
    constexpr auto EPSILON = std::numeric_limits<float>::min();
    // Avoid division by zero
    direction.x = std::copysign(glm::max(glm::abs(direction.x), EPSILON), direction.x);
    direction.y = std::copysign(glm::max(glm::abs(direction.y), EPSILON), direction.y);
    direction.z = std::copysign(glm::max(glm::abs(direction.z), EPSILON), direction.z);
    auto const coords_steps = glm::ivec3{ glm::sign(direction) };
    auto coords = glm::ivec3{ a };

    auto const fract = glm::fract(a);
    auto const straight_distances = glm::mix(fract, 1.f - fract, glm::vec3{ coords_steps + 1 } / 2.f);
    auto distances = glm::vec3{
        glm::length((straight_distances.x / direction.x) * direction),
        glm::length((straight_distances.y / direction.y) * direction),
        glm::length((straight_distances.z / direction.z) * direction)
    };
    auto distances_steps = glm::vec3{
        glm::length((1.f / direction.x) * direction),
        glm::length((1.f / direction.y) * direction),
        glm::length((1.f / direction.z) * direction)
    };

    fn(coords);
    auto const last_coords = glm::ivec3{ b };
    while (coords != last_coords) {
        if (distances.x < distances.y) {
            if (distances.z < distances.x) {
                coords.z += coords_steps.z;
                distances.z += distances_steps.z;
            } else {
                coords.x += coords_steps.x;
                distances.x += distances_steps.x;
            }
        } else {
            if (distances.z < distances.y) {
                coords.z += coords_steps.z;
                distances.z += distances_steps.z;
            } else {
                coords.y += coords_steps.y;
                distances.y += distances_steps.y;
            }
        }
        fn(coords);
    }
};

bool voxelize_model(std::filesystem::path const& path, uint32_t const side_voxel_count,
    std::function<void(glm::uvec3 const&)> const& voxel_importer) {
    auto importer = Assimp::Importer{};
    importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_NORMALS | aiComponent_TANGENTS_AND_BITANGENTS
        | aiComponent_COLORS | aiComponent_TEXCOORDS | aiComponent_BONEWEIGHTS | aiComponent_ANIMATIONS
        | aiComponent_TEXTURES | aiComponent_LIGHTS | aiComponent_CAMERAS | aiComponent_MATERIALS);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    auto const* const scene = importer.ReadFile(string_from(path).c_str(), aiProcess_JoinIdenticalVertices
        | aiProcess_MakeLeftHanded | aiProcess_Triangulate | aiProcess_RemoveComponent | aiProcess_PreTransformVertices
        | aiProcess_SortByPType | aiProcess_DropNormals | static_cast<unsigned int>(aiProcess_GenBoundingBoxes));
    if (scene == nullptr) {
        std::cerr << "Model loading error : " << importer.GetErrorString() << std::endl;
        return false;
    }
    auto const& root_node = *scene->mRootNode;
    auto const for_each_node = [&](std::invocable<aiNode&> auto const& fn) {
        fn(root_node);
        for (auto const* const child : std::span{ root_node.mChildren, root_node.mNumChildren }) {
            fn(*child);
        }
    };
    auto const for_each_mesh = [&](std::invocable<aiMesh&> auto const& fn) {
        for_each_node([&](aiNode const& node) {
            for (auto const mesh_index : std::span{ node.mMeshes, node.mNumMeshes }) {
                fn(*scene->mMeshes[mesh_index]);
            }
        });
    };

    auto min = glm::vec3{ std::numeric_limits<float>::max()};
    auto max = glm::vec3{ std::numeric_limits<float>::lowest() };
    for_each_node([&](aiNode const& node) {
        for (auto const mesh_index : std::span{ node.mMeshes, node.mNumMeshes }) {
            auto const& mesh = *scene->mMeshes[mesh_index];
            min = glm::min(min, vec3_from_aivec3(mesh.mAABB.mMin));
            max = glm::max(max, vec3_from_aivec3(mesh.mAABB.mMax));
        }
    });
    auto const model_size = max - min;
    auto const scale = static_cast<float>(side_voxel_count) / max_component(model_size);

    auto added_voxels = std::vector<glm::ivec3>{};
    for_each_mesh([&](aiMesh const& mesh) {
        for (auto const& ai_face : std::span{ mesh.mFaces, mesh.mNumFaces }) {
            auto const a = scale * (vec3_from_aivec3(mesh.mVertices[ai_face.mIndices[0]]) - min);
            auto const b = scale * (vec3_from_aivec3(mesh.mVertices[ai_face.mIndices[1]]) - min);
            auto const c = scale * (vec3_from_aivec3(mesh.mVertices[ai_face.mIndices[2]]) - min);

            added_voxels.clear();
            auto const c_coords = glm::ivec3{ c };
            dda(b, c, [&](glm::ivec3 const& pos) {
                auto index = 0u;
                auto const dest = added_voxels.empty() ? b : pos == c_coords ? c : glm::vec3{ pos } + 0.5f;
                dda(a, dest, [&](glm::ivec3 const& voxel) {
                    if (index >= added_voxels.size()) {
                        added_voxels.emplace_back(-1);
                    }
                    if (voxel != added_voxels[index]) {
                        voxel_importer(glm::uvec3{ voxel });
                        added_voxels[index] = voxel;
                    }
                    index += 1u;
                });
            });
        }
    });
    return true;
}
