#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Core/Log.h>

#include <format>

#pragma comment(lib, "D3DCompiler.lib")

namespace Engine
{
    bool CompileShader(const std::filesystem::path& path, const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& shader)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, target, flags, 0, &shader, &errors);
        if (errors)
        {
            Engine::Log::Error(static_cast<const char*>(errors->GetBufferPointer()));
        }
        if (FAILED(result))
        {
            Log::Error(std::format("{} failed: 0x{:08X}", entry, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }
}
