#pragma once
#include <Engine/Platform/Window.h>
#include <Engine/Graphics/Materials/PostEffectSettings.h>
#include <d3d12.h>
#include <array>
#include <filesystem>
#include <memory>

namespace Engine
{
    class PostEffectRenderer final
    {
    public:
        /// <summary>ポストエフェクトの未初期化状態を作ります。</summary>
        PostEffectRenderer();
        /// <summary>GPU完了後に描画先・パイプラインを解放します。</summary>
        ~PostEffectRenderer();
        PostEffectRenderer(const PostEffectRenderer&)=delete;
        PostEffectRenderer& operator=(const PostEffectRenderer&)=delete;
        /// <summary>フルスクリーンのパイプラインを作成します。失敗時は再試行できます。</summary>
        bool Initialize(ID3D12Device* device,const std::filesystem::path& shader);
        /// <summary>描画可能なパイプラインがあるか確認します。</summary>
        bool Ready() const noexcept;
        /// <summary>Render内で元の出力先を保存し、線形HDRのシーン描画を開始します。</summary>
        bool Begin(ID3D12GraphicsCommandList* commands,UINT width,UINT height,const std::array<float,4>& linearClear);
        /// <summary>HDR描画を終え、ブルーム・露出・トーンマッピングを適用して元の出力先へ戻します。</summary>
        bool End(ID3D12GraphicsCommandList* commands,const PostEffectSettings& settings);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
