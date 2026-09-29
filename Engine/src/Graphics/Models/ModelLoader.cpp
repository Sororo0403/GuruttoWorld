#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Core/Log.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <format>
#include <utility>

namespace Engine
{
    bool ModelLoader::Load(const std::filesystem::path& path, std::vector<MeshData>& meshes)
    {
        Assimp::Importer importer;
        const auto utf8Path = path.u8string();
        const aiScene* scene = importer.ReadFile(reinterpret_cast<const char*>(utf8Path.c_str()),
            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals |
            aiProcess_ConvertToLeftHanded | aiProcess_PreTransformVertices | aiProcess_SortByPType |
            aiProcess_ValidateDataStructure);
        if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
        {
            Log::Error(std::format("Model import failed: {}", importer.GetErrorString()));
            return false;
        }
        std::vector<MeshData> loaded;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh& source = *scene->mMeshes[meshIndex];
            if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 || !source.HasPositions() || !source.HasNormals())
            {
                continue;
            }
            MeshData mesh;
            aiColor3D diffuse(1.0f, 1.0f, 1.0f);
            if (source.mMaterialIndex < scene->mNumMaterials)
            {
                const aiMaterial& material = *scene->mMaterials[source.mMaterialIndex];
                material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
                aiString texture;
                if (material.GetTexture(aiTextureType_DIFFUSE, 0, &texture) == AI_SUCCESS)
                {
                    if (texture.length > 0 && texture.C_Str()[0] == '*')
                    {
                        Log::Error("Embedded model textures are not supported.");
                        return false;
                    }
                    mesh.texturePath = (path.parent_path() / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(texture.C_Str())))).lexically_normal();
                }
            }
            mesh.vertices.reserve(source.mNumVertices);
            for (unsigned int i = 0; i < source.mNumVertices; ++i)
            {
                const auto& position = source.mVertices[i];
                const auto& normal = source.mNormals[i];
                const aiVector3D uv = source.HasTextureCoords(0) ? source.mTextureCoords[0][i] : aiVector3D{};
                mesh.vertices.push_back({ {position.x, position.y, position.z}, {normal.x, normal.y, normal.z},
                    {uv.x, uv.y}, {diffuse.r, diffuse.g, diffuse.b, 1.0f} });
            }
            mesh.indices.reserve(static_cast<size_t>(source.mNumFaces) * 3);
            for (unsigned int i = 0; i < source.mNumFaces; ++i)
            {
                const aiFace& face = source.mFaces[i];
                if (face.mNumIndices != 3)
                {
                    Log::Error("Model contains a non-triangle face after triangulation.");
                    return false;
                }
                mesh.indices.insert(mesh.indices.end(), face.mIndices, face.mIndices + 3);
            }
            if (!mesh.indices.empty())
            {
                loaded.push_back(std::move(mesh));
            }
        }
        if (loaded.empty())
        {
            Log::Error("Model contains no drawable triangles.");
            return false;
        }
        Log::Info(std::format("OBJ loaded: {} meshes.", loaded.size()));
        meshes = std::move(loaded);
        return true;
    }
}
