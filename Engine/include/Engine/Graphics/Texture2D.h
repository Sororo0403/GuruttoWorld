#pragma once

#include <Engine/Platform/Window.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <filesystem>
#include <vector>

namespace Engine
{
    class Texture2D final
    {
    public:
        /// <summary>
        /// 2D テクスチャのリソース管理を初期化します。
        /// </summary>
        Texture2D() = default;

        /// <summary>
        /// テクスチャを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~Texture2D() = default;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        Texture2D(const Texture2D&) = delete;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        Texture2D& operator=(const Texture2D&) = delete;

        /// <summary>
        /// 画像を RGBA8 として読み込み、GPU へ転送して SRV を生成します。転送完了まで待機します。
        /// </summary>
        /// <param name="device">リソースを生成するデバイス。</param>
        /// <param name="queue">デバイスと対応する DIRECT 型のコマンドキュー。</param>
        /// <param name="path">PNG など WIC が対応する画像ファイル。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& path);

        /// <summary>
        /// シェーダーから参照するヒープを取得します。所有権は移譲しません。
        /// </summary>
        /// <returns>初期化済みのヒープ。未初期化の場合は nullptr。</returns>
        ID3D12DescriptorHeap* GetDescriptorHeap() const noexcept;

        /// <summary>
        /// 初期化済みテクスチャの SRV を指す GPU ハンドルを取得します。
        /// </summary>
        /// <returns>SRV の GPU ハンドル。未初期化の場合は空のハンドル。</returns>
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const noexcept;

    private:
        struct ImageData
        {
            UINT width = 0;
            UINT height = 0;
            std::vector<unsigned char> pixels;
        };

        /// <summary>
        /// WIC を使用して画像の先頭フレームを RGBA8 に変換します。
        /// </summary>
        /// <param name="path">画像ファイルのパス。</param>
        /// <param name="image">幅、高さ、画素データの出力先。</param>
        /// <returns>読み込みと変換に成功した場合は true。</returns>
        bool LoadImage(const std::filesystem::path& path, ImageData& image);

        /// <summary>
        /// DEFAULT ヒープにコピー先状態のテクスチャを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="image">画像のサイズ情報。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateResource(ID3D12Device* device, const ImageData& image);

        /// <summary>
        /// 行ピッチの配置規則に従い画素をアップロードバッファーへ書き込みます。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="image">転送する画素。</param>
        /// <param name="upload">アップロードバッファーの出力先。</param>
        /// <param name="footprint">テクスチャ転送時の配置情報の出力先。</param>
        /// <returns>生成と書き込みに成功した場合は true。</returns>
        bool CreateUploadBuffer(ID3D12Device* device, const ImageData& image,
            Microsoft::WRL::ComPtr<ID3D12Resource>& upload, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

        /// <summary>
        /// 画素のコピーとシェーダー参照状態への遷移を実行し、フェンスで完了を待機します。
        /// </summary>
        /// <param name="device">コマンドと同期オブジェクトを生成するデバイス。</param>
        /// <param name="queue">転送コマンドを送信する描画キュー。</param>
        /// <param name="upload">コピー元のアップロードバッファー。</param>
        /// <param name="footprint">コピー元の配置情報。</param>
        /// <returns>転送と待機に成功した場合は true。</returns>
        bool UploadAndWait(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* upload,
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint);

        /// <summary>
        /// 描画用の SRV ヒープとテクスチャビューを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateShaderResourceView(ID3D12Device* device);

        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
    };
}
