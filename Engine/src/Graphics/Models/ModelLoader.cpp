#include <Engine/Graphics/Models/ModelLoader.h>
#include <Engine/Core/Log.h>
#include <Engine/Assets/AssetDatabase.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <format>
#include <utility>
#include <Engine/Core/Profiler.h>
#include <assimp/DefaultIOSystem.h>
#include <mutex>

namespace
{
    using Dependencies = std::map<std::filesystem::path, std::optional<std::string>>;

    // 存在しなかった任意の関連ファイルも記録し、後から追加された場合に再取り込みします。
    class ImportIo final : public Assimp::IOSystem
    {
    public:
        mutable Dependencies dependencies;
        mutable bool cacheable = true;

        /// <summary>標準の存在確認と、その時点のファイル内容の記録を行います。</summary>
        bool Exists(const char* file) const override
        {
            Record(file);
            return io_.Exists(file);
        }
        /// <summary>標準のパス区切り文字を返します。</summary>
        char getOsSeparator() const override
        {
            return io_.getOsSeparator();
        }
        /// <summary>取り込み時に参照するファイルを記録して開きます。</summary>
        Assimp::IOStream* Open(const char* file, const char* mode = "rb") override
        {
            Record(file);
            return io_.Open(file, mode);
        }
        /// <summary>標準のストリーム解放処理へ委譲します。</summary>
        void Close(Assimp::IOStream* stream) override
        {
            io_.Close(stream);
        }
        /// <summary>標準のパス同一性判定へ委譲します。</summary>
        bool ComparePaths(const char* first, const char* second) const override
        {
            return io_.ComparePaths(first, second);
        }

    private:
        Assimp::DefaultIOSystem io_;
        // 記録の失敗で本来のインポート動作を変えず、その結果の共有だけを無効にします。
        void Record(const char* file) const
        {
            try
            {
                const auto path = std::filesystem::absolute(
                                      std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(file))))
                                      .lexically_normal();
                const auto contents = Engine::FileContents::Read(path);
                const auto found = dependencies.find(path);
                if (found != dependencies.end() && found->second != contents)
                    cacheable = false;
                dependencies.insert_or_assign(path, contents);
            }
            catch (const std::exception&)
            {
                cacheable = false;
            }
        }
    };

    struct ImportedModel
    {
        float scale = 1;
        bool flipV = false;
        Dependencies dependencies;
        std::vector<Engine::MeshData> meshes;
        size_t bytes = 0;
    };
    struct ImportEntry
    {
        std::shared_ptr<const ImportedModel> model;
        uint64_t used = 0;
    };
    std::mutex importMutex;
    std::map<std::filesystem::path, ImportEntry> imports;
    size_t importBytes = 0;
    uint64_t importSequence = 0;

    bool Unchanged(const Dependencies& dependencies)
    {
        try
        {
            for (const auto& [path, contents] : dependencies)
                if (Engine::FileContents::Read(path) != contents)
                    return false;
            return !dependencies.empty();
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool ReadMaterial(const aiScene& scene, const aiMesh& source, const std::filesystem::path& path,
                      Engine::MeshData& mesh, aiColor3D& diffuse)
    {
        if (source.mMaterialIndex < scene.mNumMaterials)
        {
            const aiMaterial& material = *scene.mMaterials[source.mMaterialIndex];
            material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
            aiString texture;
            if (material.GetTexture(aiTextureType_DIFFUSE, 0, &texture) == AI_SUCCESS)
            {
                if (texture.length > 0 && texture.C_Str()[0] == '*')
                {
                    Engine::Log::Error("Embedded model textures are not supported.");
                    return false;
                }
                mesh.texturePath =
                    (path.parent_path() /
                     std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(texture.C_Str()))))
                        .lexically_normal();
            }
        }
        return true;
    }
    bool ReadGeometry(const aiMesh& source, const Engine::AssetMetadata& settings, const aiColor3D& diffuse,
                      Engine::MeshData& mesh)
    {
        mesh.vertices.reserve(source.mNumVertices);
        for (unsigned int i = 0; i < source.mNumVertices; ++i)
        {
            const auto& position = source.mVertices[i];
            const auto& normal = source.mNormals[i];
            const aiVector3D uv = source.HasTextureCoords(0) ? source.mTextureCoords[0][i] : aiVector3D{};
            mesh.vertices.push_back(
                {{position.x * settings.scale, position.y * settings.scale, position.z * settings.scale},
                 {normal.x, normal.y, normal.z},
                 {uv.x, settings.flipV ? 1 - uv.y : uv.y},
                 {diffuse.r, diffuse.g, diffuse.b, 1.0f}});
        }
        mesh.indices.reserve(static_cast<size_t>(source.mNumFaces) * 3);
        for (unsigned int i = 0; i < source.mNumFaces; ++i)
        {
            const aiFace& face = source.mFaces[i];
            if (face.mNumIndices != 3)
            {
                Engine::Log::Error("Model contains a non-triangle face after triangulation.");
                return false;
            }
            mesh.indices.insert(mesh.indices.end(), face.mIndices, face.mIndices + 3);
        }
        return true;
    }

    bool ResolveTextures(const std::filesystem::path& path, std::vector<Engine::MeshData>& loaded)
    {
        using namespace Engine;
        try
        {
            const auto root = AssetDatabase::Root(path);
            if (!root.empty())
            {
                AssetDatabase database(root);
                for (auto& mesh : loaded)
                    if (!mesh.texturePath.empty())
                        mesh.texturePath =
                            root /
                            database.Resolve(std::filesystem::absolute(mesh.texturePath).lexically_relative(root));
            }
        }
        catch (const std::exception& error)
        {
            Log::Error(error.what());
            return false;
        }
        return true;
    }

}

namespace Engine
{
    bool ModelLoader::Load(const std::filesystem::path& path, std::vector<MeshData>& meshes)
    {
        AssetMetadata settings;
        try
        {
            settings = AssetDatabase::Read(path);
        }
        catch (const std::exception& error)
        {
            Log::Error(error.what());
            return false;
        }
        const auto key = std::filesystem::absolute(path).lexically_normal();
        std::shared_ptr<const ImportedModel> cached;
        {
            std::lock_guard lock(importMutex);
            if (const auto found = imports.find(key); found != imports.end())
            {
                cached = found->second.model;
                found->second.used = ++importSequence;
            }
        }
        if (cached && cached->scale == settings.scale && cached->flipV == settings.flipV &&
            Unchanged(cached->dependencies))
        {
            CpuScope scope("Model import cache hit");
            auto loaded = cached->meshes;
            if (!ResolveTextures(path, loaded))
                return false;
            meshes = std::move(loaded);
            return true;
        }
        CpuScope importScope("Model import");
        Assimp::Importer importer;
        auto* io = new ImportIo;
        importer.SetIOHandler(io);
        const auto utf8Path = path.u8string();
        const aiScene* scene =
            importer.ReadFile(reinterpret_cast<const char*>(utf8Path.c_str()),
                              aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals |
                                  aiProcess_ConvertToLeftHanded | aiProcess_PreTransformVertices |
                                  aiProcess_SortByPType | aiProcess_ValidateDataStructure);
        if (scene == nullptr || scene->mRootNode == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0)
        {
            Engine::Log::Error(std::format("Model import failed: {}", importer.GetErrorString()));
            return false;
        }
        std::vector<MeshData> loaded;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh& source = *scene->mMeshes[meshIndex];
            if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0 || !source.HasPositions() ||
                !source.HasNormals())
            {
                continue;
            }
            MeshData mesh;
            aiColor3D diffuse(1.0f, 1.0f, 1.0f);
            if (!ReadMaterial(*scene, source, path, mesh, diffuse))
                return false;
            if (!ReadGeometry(source, settings, diffuse, mesh))
                return false;
            if (!mesh.indices.empty())
            {
                loaded.push_back(std::move(mesh));
            }
        }
        if (loaded.empty())
        {
            Engine::Log::Error("Model contains no drawable triangles.");
            return false;
        }
        Log::Info(std::format("OBJ loaded: {} meshes.", loaded.size()));
        // インポート中にも変更がなかった結果だけを、CPU メッシュとして共有します。
        if (io->cacheable && Unchanged(io->dependencies))
        {
            auto entry = std::make_shared<ImportedModel>();
            entry->scale = settings.scale;
            entry->flipV = settings.flipV;
            entry->dependencies = std::move(io->dependencies);
            entry->meshes = loaded;
            for (const auto& mesh : loaded)
                entry->bytes += mesh.vertices.size() * sizeof(MeshVertex) + mesh.indices.size() * sizeof(uint32_t);
            for (const auto& [dependency, contents] : entry->dependencies)
                entry->bytes += contents ? contents->size() : 0;
            constexpr size_t Budget = 64 * 1024 * 1024;
            std::lock_guard lock(importMutex);
            if (const auto old = imports.find(key); old != imports.end())
            {
                importBytes -= old->second.model->bytes;
                imports.erase(old);
            }
            if (entry->bytes <= Budget)
            {
                // シーン全体の再取り込みを避け、件数・容量を超えた分だけ古い項目から退避します。
                constexpr size_t MaximumEntries = 128;
                while (!imports.empty() && (imports.size() >= MaximumEntries || importBytes + entry->bytes > Budget))
                {
                    const auto oldest = std::min_element(imports.begin(), imports.end(),
                                                         [](const auto& a, const auto& b)
                                                         {
                                                             return a.second.used < b.second.used;
                                                         });
                    importBytes -= oldest->second.model->bytes;
                    imports.erase(oldest);
                }
                importBytes += entry->bytes;
                imports.emplace(key, ImportEntry{std::move(entry), ++importSequence});
            }
        }
        if (!ResolveTextures(path, loaded))
            return false;
        meshes = std::move(loaded);
        return true;
    }
}
