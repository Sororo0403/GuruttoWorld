#include <Engine/Core/FileContents.h>

namespace Engine
{
    auto FileContents::Read(const std::filesystem::path& path) -> std::optional<std::string>
    {
        if (!std::filesystem::exists(path))
            return std::nullopt;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Not a regular file: " + Text(path));
        auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            Failure("open", path, GetLastError());
        try
        {
            std::string contents;
            char buffer[65536];
            DWORD count = 0;
            while (true)
            {
                if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr))
                    Failure("read", path, GetLastError());
                if (!count)
                    break;
                contents.append(buffer, count);
            }
            CloseHandle(file);
            return contents;
        }
        catch (...)
        {
            CloseHandle(file);
            throw;
        }
    }

    auto FileContents::Write(const std::filesystem::path& path, std::string_view contents, bool overwrite) -> void
    {
        static std::atomic<unsigned long long> sequence{0};
        auto temporary = path;
        temporary += L".wp1-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
                     L"-" + std::to_wstring(sequence++) + L".tmp";
        HANDLE file =
            CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            Failure("create", path, GetLastError());
        try
        {
            if (GetFileType(file) != FILE_TYPE_DISK)
                Failure("create regular", path, ERROR_INVALID_NAME);
            size_t offset = 0;
            while (offset < contents.size())
            {
                const auto size = static_cast<DWORD>((std::min)(contents.size() - offset, size_t(1024 * 1024)));
                DWORD written = 0;
                if (!WriteFile(file, contents.data() + offset, size, &written, nullptr) || written == 0)
                    Failure("write", path, GetLastError());
                offset += written;
            }
            if (!FlushFileBuffers(file))
                Failure("flush", path, GetLastError());
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            if (!MoveFileExW(temporary.c_str(), path.c_str(),
                             MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
                Failure("replace", path, GetLastError());
        }
        catch (...)
        {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

    auto FileContents::Remove(const std::filesystem::path& path) -> void
    {
        if (!std::filesystem::exists(path))
            return;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Not a regular file: " + Text(path));
        std::filesystem::remove(path);
    }
}
