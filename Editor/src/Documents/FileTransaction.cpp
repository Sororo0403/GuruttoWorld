#include "FileTransaction.h"
#include <Engine/Core/FileReplacement.h>

namespace Editor
{
    auto FileTransaction::Prepare(const std::filesystem::path& path, std::optional<std::string> after) -> FileChange
    {
        const auto absolute = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        Engine::FileJournal::Recover(Engine::FileSaveLock::Root(absolute));
        return {absolute, Engine::FileContents::Read(absolute), std::move(after)};
    }

    auto FileTransaction::Apply(const std::vector<FileChange>& changes, bool forward, std::string& error,
                                const std::function<bool()>& commitDocument) -> bool
    {
        error.clear();
        try
        {
            Validate(changes);
            std::vector<Engine::FileReplacement::Change> replacements;
            for (const auto& change : changes)
                replacements.push_back(
                    {change.path, forward ? change.before : change.after, forward ? change.after : change.before});
            return Engine::FileReplacement::ApplyAll(replacements, error, commitDocument);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
        }
        return false;
    }

    auto FileTransaction::Validate(const std::vector<FileChange>& changes) -> void
    {
        std::vector<std::wstring> targets;
        for (const auto& change : changes)
        {
            if (!change.path.is_absolute())
                throw std::runtime_error("File transaction requires absolute paths");
            for (const auto& part : change.path.relative_path())
                if (part.native().find(L':') != std::wstring::npos)
                    throw std::runtime_error("File transaction cannot target an alternate stream");
            const auto target = std::filesystem::weakly_canonical(change.path).native();
            if (CompareStringOrdinal(change.path.lexically_normal().c_str(), -1, target.c_str(), -1, TRUE) !=
                CSTR_EQUAL)
                throw std::runtime_error("File transaction path was redirected");
            for (const auto& previous : targets)
                if (CompareStringOrdinal(previous.c_str(), -1, target.c_str(), -1, TRUE) == CSTR_EQUAL)
                    throw std::runtime_error("Duplicate file transaction target");
            targets.push_back(target);
        }
    }
}
