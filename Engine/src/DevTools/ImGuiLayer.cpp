#include <Engine/DevTools/ImGuiLayer.h>
#include <Engine/Core/Log.h>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include <cassert>
#include <format>
#include <stdexcept>

// Win32 バックエンドのヘッダーでは Windows 型への依存を避けるため宣言されていません。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Engine
{
    ImGuiLayer::~ImGuiLayer()
    {
        Shutdown();
    }

    bool ImGuiLayer::Initialize(HWND window, ID3D12Device* device, ID3D12CommandQueue* queue,
        int framesInFlight, DXGI_FORMAT format)
    {
        if (context_ != nullptr || ImGui::GetCurrentContext() != nullptr ||
            !IsWindow(window) || device == nullptr || queue == nullptr || framesInFlight <= 0)
        {
            Log::Error("Invalid ImGui initialization arguments or context already exists.");
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = DescriptorCount;
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        const HRESULT result = device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&descriptorHeap_));
        if (FAILED(result))
        {
            Log::Error(std::format("Create ImGui descriptor heap failed: 0x{:08X}", static_cast<unsigned long>(result)));
            return false;
        }
        descriptorSize_ = device->GetDescriptorHandleIncrementSize(description.Type);
        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
        platformInitialized_ = ImGui_ImplWin32_Init(window);
        if (!platformInitialized_)
        {
            Log::Error("Initialize ImGui Win32 backend failed.");
            Shutdown();
            return false;
        }

        ImGui_ImplDX12_InitInfo info{};
        info.Device = device;
        info.CommandQueue = queue;
        info.NumFramesInFlight = framesInFlight;
        info.RTVFormat = format;
        info.SrvDescriptorHeap = descriptorHeap_.Get();
        info.UserData = this;
        info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* data,
            D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
        {
            static_cast<ImGuiLayer*>(data->UserData)->AllocateDescriptor(*cpu, *gpu);
        };
        info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* data,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
        {
            static_cast<ImGuiLayer*>(data->UserData)->FreeDescriptor(cpu);
        };
        rendererInitialized_ = ImGui_ImplDX12_Init(&info);
        if (!rendererInitialized_ || !ImGui_ImplDX12_CreateDeviceObjects())
        {
            Log::Error("Initialize ImGui DirectX 12 backend failed.");
            Shutdown();
            return false;
        }
        Log::Info("Dear ImGui initialized (Debug/Development).");
        return true;
    }

    void ImGuiLayer::Shutdown()
    {
        if (context_ != nullptr)
        {
            ImGui::SetCurrentContext(context_);
            if (rendererInitialized_)
            {
                ImGui_ImplDX12_Shutdown();
                rendererInitialized_ = false;
            }
            if (platformInitialized_)
            {
                ImGui_ImplWin32_Shutdown();
                platformInitialized_ = false;
            }
            ImGui::DestroyContext(context_);
            context_ = nullptr;
        }
        descriptorHeap_.Reset();
        sceneCpu_ = {};
        sceneGpu_ = {};
        descriptorSize_ = 0;
        allocated_.fill(false);
    }

    void ImGuiLayer::BeginFrame()
    {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::Render(ID3D12GraphicsCommandList* commands)
    {
        ImGui::Render();
        ID3D12DescriptorHeap* heaps[] = { descriptorHeap_.Get() };
        commands->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ImGuiLayer::SetSceneTexture(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE source)
    {
        if (!rendererInitialized_ || !device || !source.ptr) return {};
        if (!sceneCpu_.ptr) AllocateDescriptor(sceneCpu_, sceneGpu_);
        device->CopyDescriptorsSimple(1, sceneCpu_, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return sceneGpu_;
    }

    bool ImGuiLayer::ProcessMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendPlatformUserData != nullptr &&
            ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam) != 0;
    }

    void ImGuiLayer::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
    {
        for (UINT index = 0; index < DescriptorCount; ++index)
        {
            if (!allocated_[index])
            {
                allocated_[index] = true;
                cpu = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
                gpu = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
                cpu.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
                gpu.ptr += static_cast<UINT64>(index) * descriptorSize_;
                return;
            }
        }
        throw std::runtime_error("ImGui descriptor heap is full.");
    }

    void ImGuiLayer::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
    {
        const SIZE_T offset = cpu.ptr - descriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
        const SIZE_T index = offset / descriptorSize_;
        assert(offset % descriptorSize_ == 0 && index < allocated_.size() && allocated_[index]);
        allocated_[index] = false;
    }
}
