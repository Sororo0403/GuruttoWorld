#pragma once
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <map>
#include <string>
#include <vector>

namespace Engine
{
    struct Particle
    {
        std::array<float, 3> position{};
        std::array<float, 3> velocity{};
        std::array<float, 4> color{ 1, 1, 1, 1 };
        float size = 0.1f;
        float lifetime = 1.0f;
        float age = 0.0f;
    };

    class ParticleSystem final
    {
    public:
        /// <summary>
        /// 共有テクスチャで名前付きグループを生成します。同名・不正入力は false を返します。
        /// </summary>
        bool CreateGroup(const std::string& name, ID3D12Device* device, ID3D12CommandQueue* queue,
            std::shared_ptr<const Texture2D> texture, const std::filesystem::path& shaderPath);
        /// <summary>
        /// ワールド発生座標・速度・寿命などを指定して一粒発生させます。不正入力・上限到達時は false です。
        /// </summary>
        bool Emit(const std::string& group, const Particle& particle);
        /// <summary>
        /// 経過秒数で移動・寿命を更新します。不正時間は無視します。
        /// </summary>
        void Update(double deltaSeconds);
        /// <summary>
        /// 共通カメラを向く板ポリゴンで全グループを描画します。GPU 完了後に本クラスを破棄してください。
        /// </summary>
        void Draw(ID3D12GraphicsCommandList* commands, const Camera& camera) const;
        /// <summary>
        /// 指定グループの生存数を取得します。未登録の場合はゼロです。
        /// </summary>
        size_t GetParticleCount(const std::string& group) const;
    private:
        struct Group
        {
            ParticleRenderer renderer;
            std::vector<Particle> particles;
        };
        static constexpr size_t MaxParticlesPerGroup = 1024;
        std::map<std::string, std::unique_ptr<Group>> groups_;
    };
}
