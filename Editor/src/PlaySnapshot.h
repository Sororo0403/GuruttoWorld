#pragma once
#include "EditState.h"
#include "EditHistory.h"
#include "SceneDocument.h"

namespace Editor
{
    class PlaySnapshot final
    {
    public:
        PlaySnapshot(const SceneRuntime::SceneWorld& world, const EditState& state,
            const EditHistory& history, const SceneDocument& document) :
            json_(world.Layout().Serialize()), state_(state), history_(history), document_(document) {}
        // Caller must wait for GPU idle. An unchanged edit world needs no asset reload.
        bool Restore(SceneRuntime::SceneWorld& world, const std::filesystem::path& root, EditState& state,
            EditHistory& history, SceneDocument& document, std::string& error) const
        {
            try
            {
                auto restoredState=state_;
                auto restoredHistory=history_;
                auto restoredDocument=document_;
                if (world.Layout().Serialize()!=json_ &&
                    !world.ReplaceLayout(SceneRuntime::SceneLayout::Parse(json_),root,error)) return false;
                state=std::move(restoredState);
                history=std::move(restoredHistory);
                document=std::move(restoredDocument);
                error.clear();
                return true;
            }
            catch (const std::exception& exception) { error=exception.what(); return false; }
        }
    private:
        std::string json_;
        EditState state_;
        EditHistory history_;
        SceneDocument document_;
    };
}
