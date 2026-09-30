#include <Engine/Core/DiagnosticPaths.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/CrashHandler.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include "../App/src/Scenes/TitleScene.h"
#include <ShlObj.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
    using Microsoft::WRL::ComPtr;
    void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
    void Hr(HRESULT result) { Check(SUCCEEDED(result), "D3D12 call failed"); }

    void CheckGpuMessages(ID3D12Device* device)
    {
        ComPtr<ID3D12InfoQueue> messages;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&messages)))) return;
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0;
            Hr(messages->GetMessage(i, nullptr, &size));
            std::vector<unsigned char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Hr(messages->GetMessage(i, message, &size));
            Check(message->Severity > D3D12_MESSAGE_SEVERITY_ERROR, message->pDescription);
        }
    }

    void ValidateDiagnostics()
    {
        PWSTR directory = nullptr;
        Hr(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &directory));
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owner(directory, CoTaskMemFree);
        Check(Engine::GetDiagnosticsRoot() == std::filesystem::path(directory) / "WP1", "user diagnostics path");
        // ファイルを作る検証では明示パスを使い、ユーザーの診断ファイルを変更しません。
        const auto folder = std::filesystem::path("generated/tests/diagnostics") / std::to_string(GetTickCount64());
        const auto log = folder / "custom.log";
        Check(Engine::Log::Initialize(log), "custom log path");
        Engine::Log::Info("diagnostic regression marker");
        Engine::Log::Shutdown();
        std::ifstream stream(log);
        const std::string contents{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        Check(contents.find("diagnostic regression marker") != std::string::npos, "custom log output");
        Check(Engine::CrashHandler::Initialize(folder / "crashes"), "custom crash path");
        Engine::CrashHandler::Shutdown();
        Check(std::filesystem::is_directory(folder / "crashes"), "crash directory created");
    }

    void ValidateTitle()
    {
        Check(Engine::Log::Initialize("generated/tests/title-rendering.log"), "title diagnostic log");
        constexpr std::array<std::array<int, 2>, 3> TitleSizes{{ {1280, 720}, {1024, 768}, {720, 1280} }};
        for (const auto& size : TitleSizes)
        {
            Engine::Window window;
            Engine::DirectX12Renderer renderer;
            Check(window.Create(L"Hidden title validation", size[0], size[1]), "title window");
            Check(renderer.Initialize(window.GetHandle()), "title renderer");
            {
                App::TitleScene title(std::filesystem::absolute("App"));
                Check(title.Initialize(renderer), "title assets and sprite pipeline");
                Check(title.Draw(renderer) != Engine::RenderResult::Failed, "title rendering");
                Check(renderer.WaitForIdle(), "title GPU completion");
            }
            CheckGpuMessages(renderer.GetDevice());
        }
        Engine::Log::Shutdown();
    }

    void ValidateMirroredMesh()
    {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Hr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        Hr(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)));

        Engine::MeshData data;
        data.vertices = {
            {{-0.75f, -0.75f, 0.5f}, {0, 0, -1}, {0, 1}},
            {{0, 0.75f, 0.5f}, {0, 0, -1}, {0.5f, 0}},
            {{0.75f, -0.75f, 0.5f}, {0, 0, -1}, {1, 1}}
        };
        data.indices = {0, 1, 2};
        Engine::MeshRenderer mesh;
        Check(mesh.Initialize(device.Get(), queue.Get(), data, "App/Shaders/Mesh.hlsl"), "mesh initialization");

        ComPtr<ID3D12Resource> target;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = description.Height = 128;
        description.DepthOrArraySize = description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)));
        ComPtr<ID3D12DescriptorHeap> targets;
        D3D12_DESCRIPTOR_HEAP_DESC targetDescription{};
        targetDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        targetDescription.NumDescriptors = 1;
        Hr(device->CreateDescriptorHeap(&targetDescription, IID_PPV_ARGS(&targets)));
        const auto rtv = targets->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(), nullptr, rtv);
        Engine::DepthBuffer depth;
        Check(depth.Initialize(device.Get(), 128, 128), "test depth buffer");
        const auto dsv = depth.GetHandle();

        constexpr UINT CaseCount = 4;
        ComPtr<ID3D12QueryHeap> queries;
        D3D12_QUERY_HEAP_DESC queryDescription{};
        queryDescription.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        queryDescription.Count = CaseCount;
        Hr(device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&queries)));
        ComPtr<ID3D12Resource> readback;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        description = {};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = sizeof(UINT64) * CaseCount;
        description.Height = description.DepthOrArraySize = description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));

        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence> fence;
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
        Hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const D3D12_VIEWPORT viewport{0, 0, 128, 128, 0, 1};
        const D3D12_RECT scissor{0, 0, 128, 128};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        DirectX::XMFLOAT4X4 identity;
        DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
        // 通常、X 鏡映、XY 鏡映、裏向き。最後のケースで裏面除去の維持も検証します。
        const std::array<DirectX::XMFLOAT3, CaseCount> scales{{{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, 1, -1}}};
        for (UINT i = 0; i < CaseCount; ++i)
        {
            const float clear[4]{};
            commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
            commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
            DirectX::XMFLOAT4X4 world;
            DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixScaling(scales[i].x, scales[i].y, scales[i].z) *
                DirectX::XMMatrixTranslation(0, 0, i == 3 ? 1.0f : 0.0f));
            commands->BeginQuery(queries.Get(), D3D12_QUERY_TYPE_OCCLUSION, i);
            mesh.Draw(commands.Get(), world, identity, {}, {0, 0, -1});
            commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_OCCLUSION, i);
        }
        commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0, CaseCount, readback.Get(), 0);
        Hr(commands->Close());
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(Engine::SignalGpuFence(device.Get(), queue.Get(), fence.Get(), 1) &&
            Engine::WaitForGpuFence(device.Get(), fence.Get(), 1, nullptr), "mirror GPU completion");
        const D3D12_RANGE range{0, sizeof(UINT64) * CaseCount};
        void* mapped = nullptr;
        Hr(readback->Map(0, &range, &mapped));
        const auto* samples = static_cast<const UINT64*>(mapped);
        const bool visible = samples[0] > 0 && samples[1] == samples[0] && samples[2] == samples[0] && samples[3] == 0;
        std::cout << "Occlusion samples: " << samples[0] << ", " << samples[1] << ", " << samples[2] << ", " << samples[3] << '\n';
        const D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
        Check(visible, "mirrored fronts visible and backface culled");
        CheckGpuMessages(device.Get());
    }
}

int main()
{
    try
    {
        ValidateDiagnostics();
        ValidateTitle();
        ValidateMirroredMesh();
        std::cout << "PASS: diagnostics location/overrides, title rendering, mirrored mesh visibility and backface culling\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
