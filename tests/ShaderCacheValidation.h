#pragma once
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Renderers/MeshResources.h>
#include <filesystem>
#include <fstream>
#include <string>

/// <summary>シェーダー依存変更と失敗、メッシュパイプラインの共有範囲を検証します。</summary>
inline bool ValidateShaderCache(ID3D12Device* device, ID3D12CommandQueue* queue, std::string& error)
{
    const auto root = std::filesystem::absolute(std::filesystem::path(u8"generated/shader-cache-validation/日本語プロジェクト"));
    std::filesystem::create_directories(root / "nested");
    const auto shader = root / "test.hlsl";
    const auto include = root / std::filesystem::path(u8"nested/共通.hlsli");
    const auto write = [](const std::filesystem::path& path, const char* text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc); file << text;
    };
    const auto require = [&](bool value, const char* message)
    {
        if (!value) error = message;
        return value;
    };
    write(shader, "#include \"nested/共通.hlsli\"\nfloat4 PSMain() : SV_TARGET { return Value(); }\nfloat4 Other() : SV_TARGET { return float4(0,0,0,1); }\n");
    write(include, "float4 Value() { return float4(1,0,0,1); }\n");
    Microsoft::WRL::ComPtr<ID3DBlob> first, repeated, changed, other;
    if (!require(Engine::CompileShader(shader, "PSMain", "ps_5_0", first), "initial shader compile") ||
        !require(Engine::CompileShader(shader, "PSMain", "ps_5_0", repeated) && first.Get() == repeated.Get(), "shader bytecode shared") ||
        !require(Engine::CompileShader(shader, "Other", "ps_5_0", other) && other.Get() != first.Get(), "shader entry isolated")) return false;
    const auto stamp = std::filesystem::last_write_time(include);
    write(include, "float4 Value() { return float4(0,1,0,1); }\n");
    std::filesystem::last_write_time(include, stamp);
    if (!require(Engine::CompileShader(shader, "PSMain", "ps_5_0", changed) && changed.Get() != first.Get(), "same-size same-time include edit invalidates")) return false;
    write(include, "float4 Value() { INVALID_SHADER; }\n");
    if (!require(!Engine::CompileShader(shader, "PSMain", "ps_5_0", changed) && !changed, "invalid shader clears old result")) return false;
    std::filesystem::remove(include);
    if (!require(!Engine::CompileShader(shader, "PSMain", "ps_5_0", changed) && !changed, "missing include rejects old cache")) return false;
    write(include, "float4 Value() { return float4(1,0,0,1); }\n");
    if (!require(Engine::CompileShader(shader, "PSMain", "ps_5_0", changed), "repaired include compiles")) return false;
    if (!require(!Engine::CompileShader(shader, "PSMain", "vs_5_0", changed) && !changed, "target change is independently validated")) return false;

    Engine::MeshResources one, two, separateQueue;
    const auto meshShader = std::filesystem::absolute("Content/Shaders/Mesh.hlsl");
    if (!require(one.Initialize(device, meshShader, queue) && two.Initialize(device, meshShader, queue), "mesh resource initializes") ||
        !require(one.GetRootSignature() == two.GetRootSignature(), "mesh root signature shared")) return false;
    for (const bool hdr : {false, true}) for (const bool transparent : {false, true}) for (const bool mirrored : {false, true})
        if (!require(one.GetPipelineState(mirrored, transparent, hdr) == two.GetPipelineState(mirrored, transparent, hdr), "mesh pipeline variants shared")) return false;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> another;
    D3D12_COMMAND_QUEUE_DESC description{}; description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!require(SUCCEEDED(device->CreateCommandQueue(&description, IID_PPV_ARGS(&another))), "second queue creates") ||
        !require(separateQueue.Initialize(device, meshShader, another.Get()), "second queue resources initialize") ||
        !require(separateQueue.GetPipelineState() != one.GetPipelineState(), "separate queues isolated")) return false;
    return true;
}
