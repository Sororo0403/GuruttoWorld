#include "DocumentSave.h"

namespace Editor
{
    auto DocumentSave::Apply(std::vector<DocumentSavePlan> plans) -> void
    {
        std::vector<FileChange> files;
        files.reserve(plans.size());
        for (auto& plan : plans)
            files.push_back(std::move(plan.file));
        std::string error;
        if (!FileTransaction::Apply(files, true, error,
                                    [&]()
                                    {
                                        for (auto& plan : plans)
                                            plan.accept();
                                        return true;
                                    }))
            throw std::runtime_error(error);
    }
}
