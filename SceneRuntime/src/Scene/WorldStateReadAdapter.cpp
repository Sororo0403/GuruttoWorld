#include <SceneRuntime/Scene/WorldStateReadAdapter.h>

namespace
{
    class RuntimeReadSource final : public SceneRuntime::WorldReadView::Source
    {
    public:
        RuntimeReadSource(SceneRuntime::WorldState::ReadView view, const SceneRuntime::ScenePlacement* owner)
            : view_(view), owner_(owner)
        {
        }
        const SceneRuntime::ScenePlacement* Find(std::string_view id) const override
        {
            if (owner_ && owner_->id == id)
                return owner_;
            auto object = Object(id);
            if (!object)
                return nullptr;
            return &objects_.insert_or_assign(std::string(id), std::move(*object)).first->second;
        }
        std::optional<SceneRuntime::ScenePlacement> Object(std::string_view id) const override
        {
            if (owner_ && owner_->id == id)
                return *owner_;
            const auto entity = view_.Resolve(id);
            return entity ? view_.Object(*entity) : std::nullopt;
        }
        std::optional<SceneRuntime::LocalTransform> Transform(std::string_view id) const override
        {
            if (owner_ && owner_->id == id)
                return SceneRuntime::LocalTransform{owner_->position, owner_->rotation, owner_->scale};
            const auto entity = view_.Resolve(id);
            return entity ? view_.Transform(*entity) : std::nullopt;
        }
        std::vector<std::string> ObjectIds() const override
        {
            std::vector<std::string> ids;
            ids.reserve(view_.Entities().size());
            for (const auto entity : view_.Entities())
                ids.push_back(*view_.ObjectId(entity));
            return ids;
        }
        SceneRuntime::SceneSettings Settings() const override
        {
            return view_.Definition()->Source().settings;
        }

    private:
        SceneRuntime::WorldState::ReadView view_;
        const SceneRuntime::ScenePlacement* owner_;
        mutable std::map<std::string, SceneRuntime::ScenePlacement> objects_;
    };
}
namespace SceneRuntime
{
    WorldReadView MakeWorldReadView(WorldState::ReadView view, const ScenePlacement* callbackOwner)
    {
        return WorldReadView(std::make_shared<RuntimeReadSource>(view, callbackOwner));
    }
}
