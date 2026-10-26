#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <SceneRuntime/Scene/SceneChanges.h>
#include <DirectXMath.h>

namespace SceneRuntime
{
    // 失敗時は行列・配置の出力を保持し、診断文字列は更新します。ワールド行列には親の非均等スケールによるせん断も保持します。
    class SceneTransforms final
    {
    public:
        /// <summary>保存データを作らず、ローカル姿勢の数値だけを行列へ変換します。</summary>
        static bool Compose(const LocalTransform& transform, DirectX::XMFLOAT4X4& output);
        static bool Compose(const ScenePlacement& placement, DirectX::XMFLOAT4X4& output);
        static bool WorldToLocal(const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT4X4& parentWorld,
                                 DirectX::XMFLOAT4X4& output);
        /// <summary>
        /// 親を基準とする SRT を、layout.objects の順序でワールド行列に変換します。
        /// </summary>
        static bool Resolve(const SceneLayout& layout, std::vector<DirectX::XMFLOAT4X4>& output, std::string& error);
        /// <summary>描画用のローカル行列差分を適用して親子姿勢を解決し、物理配置は変更しません。</summary>
        static bool Resolve(const SceneLayout& layout, const std::map<std::string, DirectX::XMFLOAT4X4>& localOverrides,
                            std::vector<DirectX::XMFLOAT4X4>& output, std::string& error);
        /// <summary>検証済みの変更前配置と同順の解決済み行列から、変更した姿勢とその子孫だけを再計算します。</summary>
        static bool ResolveChanges(const SceneLayout& layout, const SceneLayout& validatedBefore,
                                   const std::vector<DirectX::XMFLOAT4X4>& previousWorldMatrices,
                                   std::vector<DirectX::XMFLOAT4X4>& output, std::string& error);
        /// <summary>
        /// アフィン行列を編集可能な SRT に分解します。せん断を拒否し、失敗時は出力を保持します。
        /// </summary>
        static bool ReadTransform(const DirectX::XMFLOAT4X4& matrix, const ScenePlacement& reference,
                                  ScenePlacement& output);
        /// <summary>保存データを作らず、符号と等価な回転を保ってローカル姿勢を分解します。</summary>
        static bool ReadTransform(const DirectX::XMFLOAT4X4& matrix, const LocalTransform& reference,
                                  LocalTransform& output);
        static bool Matches(const DirectX::XMFLOAT4X4& left, const DirectX::XMFLOAT4X4& right);
        static bool IsUsable(const DirectX::XMFLOAT4X4& matrix);
    };
}
