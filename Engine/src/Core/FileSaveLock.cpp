#include <Engine/Core/FileSaveLock.h>

namespace Engine
{
    auto FileSaveLock::Root(const std::filesystem::path& file) -> std::filesystem::path
    {
        auto parent = std::filesystem::weakly_canonical(std::filesystem::absolute(file)).parent_path();
        auto fallback = parent;
        for (auto current = parent;; current = current.parent_path())
        {
            if (std::filesystem::is_regular_file(current / "WP1.project.json") ||
                std::filesystem::is_regular_file(current / "WP1.runtime.json"))
                return current;
            // 記述ファイルのない素材用ルートでも、途中の同名フォルダーで領域を分割しません。
            if (CompareStringOrdinal(current.filename().c_str(), -1, L"Assets", -1, TRUE) == CSTR_EQUAL ||
                CompareStringOrdinal(current.filename().c_str(), -1, L"Shaders", -1, TRUE) == CSTR_EQUAL)
                fallback = current.parent_path();
            if (current == current.root_path())
                return fallback;
        }
    }

    auto FileSaveLock::IsJournalPath(const std::filesystem::path& path) -> bool
    {
        return std::any_of(path.begin(), path.end(),
                           [](const auto& part)
                           {
                               return CompareStringOrdinal(part.c_str(), -1, L".wp1-transactions", -1, TRUE) ==
                                      CSTR_EQUAL;
                           });
    }

    FileSaveLock::FileSaveLock(const std::filesystem::path& root)
        : key_(std::filesystem::weakly_canonical(std::filesystem::absolute(root)).native())
    {
        std::transform(key_.begin(), key_.end(), key_.begin(),
                       [](wchar_t c)
                       {
                           return static_cast<wchar_t>(std::towupper(c));
                       });
        outermost_ = std::find(held_.begin(), held_.end(), key_) == held_.end();
        unsigned long long hash = 14695981039346656037ull;
        for (const auto c : key_)
        {
            hash ^= static_cast<unsigned long long>(c);
            hash *= 1099511628211ull;
        }
        const auto name = L"Local\\WP1.FileSave." + std::to_wstring(hash);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_)
            throw std::runtime_error("保存の排他を作成できません");
        const auto result = WaitForSingleObject(mutex_, 10000);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        {
            CloseHandle(mutex_);
            throw std::runtime_error("別の保存または復旧が実行中です。完了後に再試行してください。");
        }
        try
        {
            held_.push_back(key_);
        }
        catch (...)
        {
            ReleaseMutex(mutex_);
            CloseHandle(mutex_);
            throw;
        }
    }

    FileSaveLock::~FileSaveLock()
    {
        held_.pop_back();
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
}
