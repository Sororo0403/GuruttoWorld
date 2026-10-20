#pragma once
#include "FileTransaction.h"

namespace Editor
{
    struct DocumentSavePlan
    {
        FileChange file;
        std::function<void()> accept;
    };

    class DocumentSave final
    {
    public:
        /// <summary>全ファイルの保存後に、準備済みの保存基準を例外なく交換します。</summary>
        static void Apply(std::vector<DocumentSavePlan> plans)
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
    };
}
