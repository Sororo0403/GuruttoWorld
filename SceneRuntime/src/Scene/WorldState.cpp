#include <SceneRuntime/Scene/WorldState.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <Engine/Core/Profiler.h>
#include <Engine/Core/Validation.h>
#include "SceneComponentJson.h"
#include <limits>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using namespace SceneRuntime;

    inline constexpr size_t ComponentPageSize = 64;
    template <class T> struct ComponentPage
    {
        using ValueType = T;
        std::vector<T> values;
    };
#define ENGINE_RUNTIME_PREPARED_OPTIONAL(Type, member, references, assets, order)                                      \
    std::shared_ptr<ComponentPage<std::optional<Type>>>,
#define ENGINE_RUNTIME_PREPARED_REPEATED(Type, member, references, assets, order)                                      \
    std::shared_ptr<ComponentPage<std::vector<Type>>>,
    using PreparedComponentPage = std::variant<ENGINE_SCENE_COMPONENTS(
        ENGINE_RUNTIME_PREPARED_OPTIONAL, ENGINE_RUNTIME_PREPARED_REPEATED) std::monostate>;
#undef ENGINE_RUNTIME_PREPARED_OPTIONAL
#undef ENGINE_RUNTIME_PREPARED_REPEATED

    struct PageKey
    {
        size_t page, type;
        bool operator==(const PageKey&) const = default;
    };
    struct PageHash
    {
        size_t operator()(const PageKey& key) const noexcept
        {
            return std::hash<size_t>{}(key.page) ^ (std::hash<size_t>{}(key.type) << 1);
        }
    };
    using PreparedPages = std::unordered_map<PageKey, PreparedComponentPage, PageHash>;

    template <class T> struct ComponentStorage
    {
        std::vector<EntityHandle> owners;
        std::vector<std::shared_ptr<ComponentPage<T>>> pages;
        std::unordered_map<std::uint32_t, size_t> indices;

        const T* Find(std::uint32_t slot) const
        {
            const auto found = indices.find(slot);
            if (found == indices.end())
                return nullptr;
            const auto index = found->second;
            return &pages[index / ComponentPageSize]->values[index % ComponentPageSize];
        }
        void Add(EntityHandle entity, T value)
        {
            const auto index = owners.size();
            if (index % ComponentPageSize == 0)
            {
                auto page = std::make_shared<ComponentPage<T>>();
                page->values.reserve(ComponentPageSize);
                pages.push_back(std::move(page));
            }
            indices.emplace(entity.slot, index);
            owners.push_back(std::move(entity));
            pages.back()->values.push_back(std::move(value));
        }
        void Prepare(std::uint32_t slot, const T& value, size_t type, PreparedPages& prepared) const
        {
            const auto index = indices.at(slot), page = index / ComponentPageSize;
            const PageKey key{page, type};
            auto found = prepared.find(key);
            if (found == prepared.end())
            {
                // Debugの文字列やmapも含め、値の構築は確定前に終えます。
                auto candidate = std::make_shared<ComponentPage<T>>(*pages[page]);
                found = prepared.emplace(key, std::move(candidate)).first;
            }
            std::get<std::shared_ptr<ComponentPage<T>>>(found->second)->values[index % ComponentPageSize] = value;
        }
        void Commit(size_t page, std::shared_ptr<ComponentPage<T>>& prepared) noexcept
        {
            pages[page].swap(prepared);
        }
    };

#define ENGINE_RUNTIME_OPTIONAL(Type, member, references, assets, order) ComponentStorage<std::optional<Type>>,
#define ENGINE_RUNTIME_REPEATED(Type, member, references, assets, order) ComponentStorage<std::vector<Type>>,
    using ComponentTables =
        std::tuple<ENGINE_SCENE_COMPONENTS(ENGINE_RUNTIME_OPTIONAL, ENGINE_RUNTIME_REPEATED) std::monostate>;
#undef ENGINE_RUNTIME_OPTIONAL
#undef ENGINE_RUNTIME_REPEATED

    struct Slot
    {
        std::uint64_t generation = 0;
        size_t definitionIndex = 0;
        bool alive = false;
        LocalTransform transform;
        EntityHandle parent;
    };
    struct ComponentBinding
    {
        std::string id;
        ComponentInstanceHandle handle;
    };
    std::uint64_t BindingKey(std::uint32_t slot, size_t type)
    {
        return (static_cast<std::uint64_t>(slot) << 32) | static_cast<std::uint64_t>(type);
    }
#define ENGINE_RUNTIME_NAME(Type, member, references, assets, order) #member,
    constexpr std::array ComponentNames{ENGINE_SCENE_COMPONENTS(ENGINE_RUNTIME_NAME, ENGINE_RUNTIME_NAME) ""};
#undef ENGINE_RUNTIME_NAME
    struct Data
    {
        std::shared_ptr<const WorldDefinition> definition;
        std::vector<Slot> slots;
        std::vector<std::vector<std::uint32_t>> children;
        std::vector<EntityHandle> order;
        std::unordered_map<std::string, EntityHandle> ids;
        ComponentTables components;
        std::unordered_map<std::uint64_t, std::vector<ComponentBinding>> bindings;
        std::unordered_map<std::uint64_t, EntityHandle> componentOwners;
    };
    struct ComponentKey
    {
        std::uint32_t slot;
        size_t type;
        bool operator==(const ComponentKey&) const = default;
    };
    struct ComponentHash
    {
        size_t operator()(const ComponentKey& key) const noexcept
        {
            return std::hash<size_t>{}(key.type) ^ (std::hash<std::uint32_t>{}(key.slot) << 1);
        }
    };

    template <class T> bool Present(const T& value)
    {
        if constexpr (requires { value.has_value(); })
            return value.has_value();
        else
            return !value.empty();
    }

#define ENGINE_RUNTIME_ATTACH(Type, member, references, assets, order)                                                 \
    void Attach(ScenePlacement& object, const std::optional<Type>& value)                                              \
    {                                                                                                                  \
        object.member = value;                                                                                         \
    }
#define ENGINE_RUNTIME_ATTACH_REPEATED(Type, member, references, assets, order)                                        \
    void Attach(ScenePlacement& object, const std::vector<Type>& value)                                                \
    {                                                                                                                  \
        object.member = value;                                                                                         \
    }
    ENGINE_SCENE_COMPONENTS(ENGINE_RUNTIME_ATTACH, ENGINE_RUNTIME_ATTACH_REPEATED)
#undef ENGINE_RUNTIME_ATTACH
#undef ENGINE_RUNTIME_ATTACH_REPEATED

    void Attach(ScenePlacement&, std::monostate) {}

    RuntimeComponentValue Value(const Data& data, std::uint32_t slot, RuntimeComponentValue empty)
    {
        return std::visit(
            [&](const auto& selector) -> RuntimeComponentValue
            {
                using T = std::decay_t<decltype(selector)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return selector;
                else
                {
                    const auto* value = std::get<ComponentStorage<T>>(data.components).Find(slot);
                    return value ? RuntimeComponentValue{*value} : RuntimeComponentValue{T{}};
                }
            },
            empty);
    }

    void CommitPages(Data& data, PreparedPages& pages) noexcept
    {
        for (auto& [key, page] : pages)
            std::visit(
                [&](auto& ready)
                {
                    using T = std::decay_t<decltype(ready)>;
                    if constexpr (!std::is_same_v<T, std::monostate>)
                    {
                        using ValueType = typename T::element_type::ValueType;
                        std::get<ComponentStorage<ValueType>>(data.components).Commit(key.page, ready);
                    }
                },
                page);
    }

    bool Alive(const Data& data, const std::shared_ptr<const WorldIdentity>& world, const EntityHandle& entity) noexcept
    {
        return entity.world == world && entity.slot < data.slots.size() && data.slots[entity.slot].alive &&
               data.slots[entity.slot].generation == entity.generation;
    }

    std::unique_ptr<Data> BuildData(std::shared_ptr<const WorldDefinition> definition, const Data* previous,
                                    const std::shared_ptr<const WorldIdentity>& world, std::uint64_t& nextGeneration)
    {
        auto data = std::make_unique<Data>();
        data->definition = std::move(definition);
        if (previous)
            data->slots = previous->slots;
        std::unordered_set<std::string_view> incoming;
        for (const auto& object : data->definition->Source().objects)
            incoming.insert(object.id);
        std::vector<std::uint32_t> free;
        for (size_t index = 0; index < data->slots.size(); ++index)
        {
            auto& slot = data->slots[index];
            if (!slot.alive || !incoming.contains(previous->definition->Source().objects[slot.definitionIndex].id))
                free.push_back(static_cast<std::uint32_t>(index));
            slot.alive = false;
        }
        for (size_t index = 0; index < data->definition->Source().objects.size(); ++index)
        {
            const auto& object = data->definition->Source().objects[index];
            EntityHandle entity;
            if (previous)
                if (const auto found = previous->ids.find(object.id); found != previous->ids.end())
                    entity = found->second;
            if (!entity)
            {
                if (nextGeneration == std::numeric_limits<std::uint64_t>::max())
                    throw std::runtime_error("Entity generation exhausted");
                std::uint32_t slot;
                if (!free.empty())
                {
                    slot = free.back();
                    free.pop_back();
                }
                else
                {
                    if (data->slots.size() >= std::numeric_limits<std::uint32_t>::max())
                        throw std::runtime_error("Entity slots exhausted");
                    slot = static_cast<std::uint32_t>(data->slots.size());
                    data->slots.emplace_back();
                }
                entity = {world, nextGeneration++, slot};
            }
            auto& slot = data->slots[entity.slot];
            slot = {entity.generation, index, true, {object.position, object.rotation, object.scale}, {}};
            data->order.push_back(entity);
            data->ids.emplace(object.id, entity);
            const auto bind = [&](const auto& values)
            {
                using T = std::decay_t<decltype(values)>;
                const auto key = BindingKey(entity.slot, RuntimeComponentValue{T{}}.index());
                auto& bindings = data->bindings[key];
                const auto append = [&](const auto& component)
                {
                    ComponentInstanceHandle instance{entity, 0};
                    if (previous && Alive(*previous, world, entity))
                        if (const auto found = previous->bindings.find(key); found != previous->bindings.end())
                            for (const auto& binding : found->second)
                                if (binding.id == component.id)
                                {
                                    instance = binding.handle;
                                    break;
                                }
                    if (!instance.generation)
                    {
                        if (nextGeneration == std::numeric_limits<std::uint64_t>::max())
                            throw std::runtime_error("Component generations exhausted");
                        instance.generation = nextGeneration++;
                    }
                    bindings.push_back({component.id, instance});
                    data->componentOwners.emplace(instance.generation, entity);
                };
                if constexpr (requires { values.has_value(); })
                    append(*values);
                else
                    for (const auto& value : values)
                        append(value);
            };
#define ENGINE_RUNTIME_ADD(Type, member, references, assets, order)                                                    \
    if (Present(object.member))                                                                                        \
    {                                                                                                                  \
        std::get<ComponentStorage<decltype(object.member)>>(data->components).Add(entity, object.member);              \
        bind(object.member);                                                                                           \
    }
            ENGINE_SCENE_COMPONENTS(ENGINE_RUNTIME_ADD, ENGINE_RUNTIME_ADD)
#undef ENGINE_RUNTIME_ADD
        }
        data->children.resize(data->slots.size());
        for (const auto entity : data->order)
        {
            const auto& object = data->definition->Source().objects[data->slots[entity.slot].definitionIndex];
            if (!object.parentId.empty())
            {
                const auto parent = data->ids.at(object.parentId);
                data->slots[entity.slot].parent = parent;
                data->children[parent.slot].push_back(entity.slot);
            }
        }
        return data;
    }

    bool ValidateVelocityChange(const RuntimeComponentValue& before, const RuntimeComponentValue& after)
    {
        const auto* previous = std::get_if<std::optional<RigidBodyComponent>>(&before);
        const auto* current = std::get_if<std::optional<RigidBodyComponent>>(&after);
        if (!previous || !*previous || !current || !*current)
            return false;
        auto unchanged = **current;
        unchanged.velocity = (**previous).velocity;
        unchanged.angularVelocity = (**previous).angularVelocity;
        if (unchanged != **previous)
            return false;
        // 形状と保存設定は検証済みの値を保ち、物理段階が更新した速度だけを再検証します。
        for (const auto* values : {&(**current).velocity, &(**current).angularVelocity})
            for (const auto value : *values)
                if (!Engine::InRange(value, -100000, 100000))
                    throw std::runtime_error("Invalid rigid body velocity");
        return true;
    }
    bool StructuralValueChange(const RuntimeComponentValue& before, const RuntimeComponentValue& after)
    {
        return std::visit(
            [&](const auto& current)
            {
                using T = std::decay_t<decltype(current)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return false;
                else
                {
                    const auto& previous = std::get<T>(before);
                    if constexpr (requires { current.has_value(); })
                        return previous.has_value() != current.has_value() ||
                               (previous && current && previous->id != current->id);
                    else
                        return previous.size() != current.size() ||
                               !std::equal(previous.begin(), previous.end(), current.begin(),
                                           [](const auto& left, const auto& right)
                                           {
                                               return left.id == right.id;
                                           });
                }
            },
            after);
    }
}

namespace SceneRuntime
{
    // ハンドルはWorld本体ではなく識別子だけを共有し、旧ハンドルが残る間の識別子再利用を防ぎます。
    struct WorldIdentity final
    {
    };

    struct WorldState::Pending
    {
        std::uint64_t id = 0;
        size_t nextId = 1;
        std::unique_ptr<Data> structure;
        std::vector<std::pair<EntityHandle, EntityHandle>> duplicates;
        std::unordered_map<std::uint32_t, LocalTransform> transforms;
        std::unordered_map<ComponentKey, RuntimeComponentValue, ComponentHash> components;
        PreparedPages pages;
        std::optional<SceneChangeSet> publication;
        SceneChangeSet delta;
        mutable std::vector<DirectX::XMFLOAT4X4> matrices;
        mutable bool matricesReady = false, matricesInitialized = false;
        mutable std::vector<unsigned char> dirty;
        mutable std::vector<std::uint32_t> chain;
        mutable size_t dirtyCount = 0;
        bool prepared = false;
    };

    struct WorldState::Impl
    {
        std::shared_ptr<const WorldIdentity> world = std::make_shared<const WorldIdentity>();
        std::uint64_t nextGeneration = 1, nextTransaction = 1;
        size_t nextId = 1;
        std::unique_ptr<Data> data;
        std::vector<DirectX::XMFLOAT4X4> matrices, matrixScratch;
        std::vector<unsigned char> dirtyScratch;
        std::vector<std::uint32_t> chainScratch;
        SceneChangeSet changes;
        Pending* active = nullptr;
        std::function<void(SceneLayout&)> prepareDefinition;

        const Data& Select(std::uint64_t transaction) const
        {
            if (!transaction)
                return *data;
            if (!active || active->id != transaction)
                throw std::logic_error("Runtime candidate view has expired");
            return active->structure ? *active->structure : *data;
        }
        LocalTransform Transform(std::uint32_t slot, std::uint64_t transaction) const
        {
            const auto& selected = Select(transaction);
            if (transaction)
                if (const auto found = active->transforms.find(slot); found != active->transforms.end())
                    return found->second;
            return selected.slots[slot].transform;
        }
        RuntimeComponentValue Component(std::uint32_t slot, RuntimeComponentValue empty,
                                        std::uint64_t transaction) const
        {
            const auto& selected = Select(transaction);
            if (transaction)
                if (const auto found = active->components.find({slot, empty.index()});
                    found != active->components.end())
                    return found->second;
            return Value(selected, slot, std::move(empty));
        }
        void MarkDirty(std::uint32_t slot) const
        {
            if (!active->matricesInitialized)
                return;
            const auto& selected = Select(active->id);
            auto& pending = *active;
            pending.chain.clear();
            pending.chain.push_back(slot);
            while (!pending.chain.empty())
            {
                const auto current = pending.chain.back();
                pending.chain.pop_back();
                if (pending.dirty[current])
                    continue;
                pending.dirty[current] = 1;
                ++pending.dirtyCount;
                pending.chain.insert(pending.chain.end(), selected.children[current].begin(),
                                     selected.children[current].end());
            }
            pending.matricesReady = pending.dirtyCount == 0;
        }
        bool InitializeCandidateMatrices(std::string& error) const
        {
            auto& pending = *active;
            if (pending.matricesInitialized)
                return true;
            const auto& selected = Select(pending.id);
            if (pending.structure)
            {
                if (!ResolveMatrices(pending.id, pending.matrices, error))
                    return false;
            }
            else
                pending.matrices = matrices;
            pending.dirty.assign(selected.slots.size(), 0);
            pending.dirtyCount = 0;
            pending.matricesInitialized = true;
            if (!pending.structure)
                for (const auto& [slot, value] : pending.transforms)
                {
                    static_cast<void>(value);
                    MarkDirty(slot);
                }
            pending.matricesReady = pending.dirtyCount == 0;
            return true;
        }
        bool ResolveCandidateMatrix(std::uint32_t slot, std::string& error) const
        {
            if (!InitializeCandidateMatrices(error))
                return false;
            auto& pending = *active;
            const auto& selected = Select(pending.id);
            pending.chain.clear();
            auto current =
                selected.order.empty() ? EntityHandle{} : EntityHandle{world, selected.slots[slot].generation, slot};
            while (current && pending.dirty[current.slot])
            {
                pending.chain.push_back(current.slot);
                current = selected.slots[current.slot].parent;
            }
            while (!pending.chain.empty())
            {
                const auto next = pending.chain.back();
                pending.chain.pop_back();
                auto& matrix = pending.matrices[next];
                if (!SceneTransforms::Compose(Transform(next, pending.id), matrix))
                {
                    error = "Invalid runtime transform";
                    return false;
                }
                if (const auto parent = selected.slots[next].parent; parent)
                    DirectX::XMStoreFloat4x4(&matrix, DirectX::XMLoadFloat4x4(&matrix) *
                                                          DirectX::XMLoadFloat4x4(&pending.matrices[parent.slot]));
                if (!SceneTransforms::IsUsable(matrix))
                {
                    error = "Invalid inherited runtime transform";
                    return false;
                }
                pending.dirty[next] = 0;
                --pending.dirtyCount;
                Engine::CpuProfiler::Add("Runtime transform recompute", 0);
            }
            pending.matricesReady = pending.dirtyCount == 0;
            return true;
        }
        bool ResolveMatrices(std::uint64_t transaction, std::vector<DirectX::XMFLOAT4X4>& output,
                             std::string& error) const
        {
            const auto& selected = Select(transaction);
            std::vector<DirectX::XMFLOAT4X4> candidate(selected.slots.size());
            std::vector<unsigned char> visited(selected.slots.size());
            std::vector<std::uint32_t> chain;
            for (const auto entity : selected.order)
            {
                auto current = entity;
                while (current && !visited[current.slot])
                {
                    if (!Alive(selected, world, current))
                    {
                        error = "Runtime parent handle is invalid";
                        return false;
                    }
                    visited[current.slot] = 1;
                    chain.push_back(current.slot);
                    current = selected.slots[current.slot].parent;
                }
                if (current && visited[current.slot] == 1)
                {
                    error = "Runtime hierarchy contains a cycle";
                    return false;
                }
                while (!chain.empty())
                {
                    const auto slot = chain.back();
                    chain.pop_back();
                    const auto& row = selected.slots[slot];
                    const auto& objectId = selected.definition->Source().objects[row.definitionIndex].id;
                    if (!SceneTransforms::Compose(Transform(slot, transaction), candidate[slot]))
                    {
                        error = "Invalid runtime transform: " + objectId;
                        return false;
                    }
                    if (row.parent)
                    {
                        DirectX::XMStoreFloat4x4(&candidate[slot],
                                                 DirectX::XMLoadFloat4x4(&candidate[slot]) *
                                                     DirectX::XMLoadFloat4x4(&candidate[row.parent.slot]));
                        if (!SceneTransforms::IsUsable(candidate[slot]))
                        {
                            error = "Invalid inherited runtime transform: " + objectId;
                            return false;
                        }
                    }
                    visited[slot] = 2;
                }
            }
            output.swap(candidate);
            error.clear();
            return true;
        }
    };

    WorldState::WorldState(std::shared_ptr<const WorldDefinition> definition) : impl_(std::make_unique<Impl>())
    {
        if (!definition)
            throw std::invalid_argument("World definition is null");
        impl_->data = BuildData(std::move(definition), nullptr, impl_->world, impl_->nextGeneration);
        std::string error;
        if (!impl_->ResolveMatrices(0, impl_->matrices, error))
            throw std::runtime_error(error);
    }
    WorldState::~WorldState() = default;

    WorldState::ReadView::ReadView(const WorldState& owner, std::uint64_t transaction) noexcept
        : owner_(&owner), transaction_(transaction)
    {
    }
    WorldState::ReadView WorldState::Read() const noexcept
    {
        return {*this, 0};
    }
    WorldState::Transaction WorldState::BeginTransaction()
    {
        return Transaction(*this);
    }

    SceneChangeSet WorldState::TakeChanges() noexcept
    {
        SceneChangeSet changes;
        if (!impl_->active)
            impl_->changes.Swap(changes);
        return changes;
    }

    bool WorldState::ReadView::Alive(EntityHandle entity) const noexcept
    {
        if (transaction_ && (!owner_->impl_->active || owner_->impl_->active->id != transaction_))
            return false;
        return ::Alive(owner_->impl_->Select(transaction_), owner_->impl_->world, entity);
    }
    bool WorldState::ReadView::Alive(ComponentInstanceHandle component) const noexcept
    {
        if (!Alive(component.entity))
            return false;
        const auto& data = owner_->impl_->Select(transaction_);
        const auto found = data.componentOwners.find(component.generation);
        return found != data.componentOwners.end() && found->second == component.entity;
    }
    std::optional<ComponentInstanceHandle> WorldState::ReadView::InstanceValue(EntityHandle entity,
                                                                               RuntimeComponentValue empty,
                                                                               std::string_view id) const
    {
        if (!Alive(entity))
            return std::nullopt;
        const auto& data = owner_->impl_->Select(transaction_);
        const auto found = data.bindings.find(BindingKey(entity.slot, empty.index()));
        if (found != data.bindings.end())
            for (const auto& binding : found->second)
                if (id.empty() || binding.id == id)
                    return binding.handle;
        return std::nullopt;
    }
    std::optional<EntityHandle> WorldState::ReadView::Resolve(std::string_view id) const
    {
        const auto& data = owner_->impl_->Select(transaction_);
        const auto found = data.ids.find(std::string(id));
        return found == data.ids.end() ? std::nullopt : std::optional{found->second};
    }
    std::span<const EntityHandle> WorldState::ReadView::Entities() const
    {
        return owner_->impl_->Select(transaction_).order;
    }
    std::optional<LocalTransform> WorldState::ReadView::Transform(EntityHandle entity) const
    {
        return Alive(entity) ? std::optional{owner_->impl_->Transform(entity.slot, transaction_)} : std::nullopt;
    }
    std::optional<EntityHandle> WorldState::ReadView::Parent(EntityHandle entity) const
    {
        if (!Alive(entity))
            return std::nullopt;
        const auto parent = owner_->impl_->Select(transaction_).slots[entity.slot].parent;
        return parent ? std::optional{parent} : std::nullopt;
    }
    std::optional<DirectX::XMFLOAT4X4> WorldState::ReadView::WorldMatrix(EntityHandle entity) const
    {
        if (!Alive(entity))
            return std::nullopt;
        auto& impl = *owner_->impl_;
        if (!transaction_)
            return impl.matrices[entity.slot];
        if (!impl.active->structure && impl.active->transforms.empty())
            return impl.matrices[entity.slot];
        std::string error;
        if (!impl.ResolveCandidateMatrix(entity.slot, error))
            return std::nullopt;
        return impl.active->matrices[entity.slot];
    }
    std::optional<std::string> WorldState::ReadView::ObjectId(EntityHandle entity) const
    {
        if (!Alive(entity))
            return std::nullopt;
        const auto& data = owner_->impl_->Select(transaction_);
        return data.definition->Source().objects[data.slots[entity.slot].definitionIndex].id;
    }
    std::vector<EntityHandle> WorldState::ReadView::FindByName(std::string_view name) const
    {
        const auto& data = owner_->impl_->Select(transaction_);
        std::vector<EntityHandle> found;
        for (const auto entity : data.order)
            if (data.definition->Source().objects[data.slots[entity.slot].definitionIndex].name == name)
                found.push_back(entity);
        return found;
    }
    RuntimeComponentValue WorldState::ReadView::Value(EntityHandle entity, RuntimeComponentValue empty) const
    {
        return Alive(entity) ? owner_->impl_->Component(entity.slot, std::move(empty), transaction_) : std::move(empty);
    }
    std::shared_ptr<const void> WorldState::ReadView::LeaseValue(EntityHandle entity, RuntimeComponentValue empty) const
    {
        if (!Alive(entity))
            return {};
        const auto& data = owner_->impl_->Select(transaction_);
        const RuntimeComponentValue* pending = nullptr;
        if (transaction_)
        {
            const auto found = owner_->impl_->active->components.find({entity.slot, empty.index()});
            if (found != owner_->impl_->active->components.end())
                pending = &found->second;
        }
        return std::visit(
            [&](const auto& selector) -> std::shared_ptr<const void>
            {
                using T = std::decay_t<decltype(selector)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return {};
                else
                {
                    if (pending)
                    {
                        const auto& value = std::get<T>(*pending);
                        if constexpr (requires { value.has_value(); })
                            return value ? std::make_shared<const typename T::value_type>(*value)
                                         : std::shared_ptr<const void>{};
                        else
                            return std::make_shared<const T>(value);
                    }
                    const auto& store = std::get<ComponentStorage<T>>(data.components);
                    const auto found = store.indices.find(entity.slot);
                    if (found == store.indices.end())
                        return {};
                    const auto index = found->second;
                    const auto& page = store.pages[index / ComponentPageSize];
                    const auto& value = page->values[index % ComponentPageSize];
                    if constexpr (requires { value.has_value(); })
                        return value ? std::shared_ptr<const void>(page, &*value) : std::shared_ptr<const void>{};
                    else
                        return std::shared_ptr<const void>(page, &value);
                }
            },
            empty);
    }
    std::span<const EntityHandle> WorldState::ReadView::WithValue(const RuntimeComponentValue& empty) const
    {
        const auto& data = owner_->impl_->Select(transaction_);
        return std::visit(
            [&](const auto& selector) -> std::span<const EntityHandle>
            {
                using T = std::decay_t<decltype(selector)>;
                if constexpr (!std::is_same_v<T, std::monostate>)
                    return std::get<ComponentStorage<T>>(data.components).owners;
                else
                    return {};
            },
            empty);
    }
    std::shared_ptr<const WorldDefinition> WorldState::ReadView::Definition() const
    {
        return owner_->impl_->Select(transaction_).definition;
    }
    std::optional<ScenePlacement> WorldState::ReadView::Object(EntityHandle entity) const
    {
        if (!Alive(entity))
            return std::nullopt;
        const auto& data = owner_->impl_->Select(transaction_);
        auto object = data.definition->Source().objects[data.slots[entity.slot].definitionIndex];
        const auto transform = *Transform(entity);
        object.position = transform.position;
        object.rotation = transform.rotation;
        object.scale = transform.scale;
        const auto parent = Parent(entity);
        object.parentId = parent ? *ObjectId(*parent) : std::string{};
#define ENGINE_RUNTIME_READ(Type, member, references, assets, order)                                                   \
    object.member = std::get<decltype(object.member)>(Value(entity, decltype(object.member){}));
        ENGINE_SCENE_COMPONENTS(ENGINE_RUNTIME_READ, ENGINE_RUNTIME_READ)
#undef ENGINE_RUNTIME_READ
        return object;
    }
    SceneLayout WorldState::ReadView::ExportLayout() const
    {
        Engine::CpuProfiler::Add("Runtime authoring export", 0);
        const auto& data = owner_->impl_->Select(transaction_);
        auto layout = data.definition->Source();
        layout.objects.clear();
        layout.objects.reserve(data.order.size());
        for (const auto entity : data.order)
            layout.objects.push_back(*Object(entity));
        return layout;
    }

    WorldState::Transaction::Transaction(WorldState& owner) : owner_(&owner), pending_(std::make_unique<Pending>())
    {
        auto& impl = *owner.impl_;
        if (impl.active)
            throw std::logic_error("Runtime transaction already active");
        pending_->id = impl.nextTransaction++;
        pending_->nextId = impl.nextId;
        pending_->matrices.swap(impl.matrixScratch);
        pending_->dirty.swap(impl.dirtyScratch);
        pending_->chain.swap(impl.chainScratch);
        impl.active = pending_.get();
    }
    WorldState::Transaction::~Transaction()
    {
        auto& impl = *owner_->impl_;
        if (impl.active == pending_.get())
            impl.active = nullptr;
        pending_->matrices.swap(impl.matrixScratch);
        pending_->dirty.swap(impl.dirtyScratch);
        pending_->chain.swap(impl.chainScratch);
    }
    WorldState::ReadView WorldState::Transaction::Read() const noexcept
    {
        return {*owner_, pending_->id};
    }

    SceneEditResult WorldState::Transaction::SetLocalTransform(EntityHandle entity, const LocalTransform& value,
                                                               std::string& error)
    {
        if (!Read().Alive(entity))
        {
            error = "Runtime entity is stale or belongs to another World";
            return {};
        }
        DirectX::XMFLOAT4X4 matrix;
        const auto finite = [](const auto& values)
        {
            return std::all_of(values.begin(), values.end(),
                               [](float number)
                               {
                                   return std::isfinite(number);
                               });
        };
        if (!finite(value.position) || !finite(value.rotation) || !finite(value.scale) ||
            !SceneTransforms::Compose(value, matrix))
        {
            error = "Invalid runtime transform";
            return {};
        }
        const auto before = *Read().Transform(entity);
        unsigned int fields = 0;
        if (before.position != value.position)
            fields |= static_cast<unsigned int>(TransformField::Position);
        if (before.rotation != value.rotation)
            fields |= static_cast<unsigned int>(TransformField::Rotation);
        if (before.scale != value.scale)
            fields |= static_cast<unsigned int>(TransformField::Scale);
        SceneEditResult result{true, {}};
        if (fields)
        {
            result.changes.transforms.push_back({*Read().ObjectId(entity), static_cast<TransformField>(fields)});
            pending_->transforms.insert_or_assign(entity.slot, value);
            pending_->prepared = false;
            pending_->matricesReady = false;
            owner_->impl_->MarkDirty(entity.slot);
        }
        error.clear();
        return result;
    }

    SceneEditResult WorldState::Transaction::SetValue(EntityHandle entity, RuntimeComponentValue value,
                                                      std::string& error)
    {
        try
        {
            if (!Read().Alive(entity))
                throw std::runtime_error("Runtime entity is stale or belongs to another World");
            const auto beforeValue = Read().Value(entity, std::visit(
                                                              [](const auto& current) -> RuntimeComponentValue
                                                              {
                                                                  return std::decay_t<decltype(current)>{};
                                                              },
                                                              value));
            if (beforeValue == value)
            {
                error.clear();
                return {true, {}};
            }
            ScenePlacement before, after;
            before.id = after.id = *Read().ObjectId(entity);
            std::visit(
                [&](const auto& component)
                {
                    Attach(before, component);
                },
                beforeValue);
            std::visit(
                [&](const auto& component)
                {
                    Attach(after, component);
                },
                value);
            if (!ValidateVelocityChange(beforeValue, value))
            {
                ScenePlacement checked;
                ReadSceneComponents(Engine::Json{{"components", WriteSceneComponents(after)}}, checked, false);
            }
            SceneEditResult result{true, {}};
            result.changes.RecordObject(before, after);
            if (StructuralValueChange(beforeValue, value) ||
                (owner_->impl_->prepareDefinition && !result.changes.resources.empty()))
            {
                auto object = *Read().Object(entity);
                std::visit(
                    [&](const auto& component)
                    {
                        Attach(object, component);
                    },
                    value);
                return ApplyStructure(ComponentsEdit{{std::move(object)}}, error);
            }
            const auto type = value.index();
            pending_->components.insert_or_assign(ComponentKey{entity.slot, type}, std::move(value));
            pending_->prepared = false;
            error.clear();
            return result;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }

    std::optional<EntityHandle> WorldState::Transaction::DuplicateSource(EntityHandle entity) const
    {
        if (!Read().Alive(entity))
            return {};
        for (auto entry = pending_->duplicates.rbegin(); entry != pending_->duplicates.rend(); ++entry)
            if (entry->second == entity)
                return entry->first;
        return {};
    }

    SceneEditResult WorldState::Transaction::ApplyStructure(const SceneEdit& edit, std::string& error)
    {
        try
        {
            const auto before = Read().ExportLayout();
            auto plan = SceneEdits::Prepare(before, edit, pending_->nextId, error);
            if (!plan)
                return {};
            SceneEditResult result{true, plan->changes};
            if (result.changes.Empty())
                return result;
            auto& impl = *owner_->impl_;
            if (impl.prepareDefinition)
            {
                impl.prepareDefinition(plan->layout);
                result.changes = SceneChangeSet::Compare(before, plan->layout);
            }
            auto definition = WorldDefinition::Compile(std::move(plan->layout));
            auto structure =
                BuildData(std::move(definition), &impl.Select(pending_->id), impl.world, impl.nextGeneration);
            auto duplicates = pending_->duplicates;
            for (const auto& [sourceId, targetId] : plan->duplicates)
            {
                const auto target = structure->ids.find(targetId);
                if (target == structure->ids.end())
                    continue;
                auto source = Read().Resolve(sourceId);
                if (!source)
                    if (const auto published = impl.data->ids.find(sourceId); published != impl.data->ids.end())
                        source = published->second;
                if (!source || *source == target->second || Read().Resolve(targetId))
                    throw std::runtime_error("Invalid runtime duplication provenance");
                duplicates.emplace_back(*source, target->second);
            }
            pending_->structure.swap(structure);
            pending_->duplicates.swap(duplicates);
            pending_->transforms.clear();
            pending_->components.clear();
            pending_->pages.clear();
            pending_->nextId = plan->nextId;
            pending_->prepared = false;
            pending_->matricesReady = false;
            pending_->matricesInitialized = false;
            error.clear();
            return result;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }

    bool WorldState::Transaction::Prepare(std::string& error)
    {
        try
        {
            auto& impl = *owner_->impl_;
            if (impl.active != pending_.get())
                throw std::logic_error("Runtime transaction has already ended");
            pending_->prepared = false;
            pending_->pages.clear();
            const auto& selected = impl.Select(pending_->id);
            for (const auto& [key, component] : pending_->components)
            {
                std::visit(
                    [&](const auto& value)
                    {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (!std::is_same_v<T, std::monostate>)
                            std::get<ComponentStorage<T>>(selected.components)
                                .Prepare(key.slot, value, key.type, pending_->pages);
                    },
                    component);
            }
            if (!pending_->matricesReady && (pending_->structure || !pending_->transforms.empty()))
            {
                if (!impl.InitializeCandidateMatrices(error))
                    return false;
                for (const auto entity : selected.order)
                    if (pending_->dirty[entity.slot] && !impl.ResolveCandidateMatrix(entity.slot, error))
                        return false;
            }
            SceneChangeSet changes;
            if (pending_->structure)
            {
                changes = SceneChangeSet::Compare(owner_->Read().ExportLayout(), Read().ExportLayout());
                for (const auto& [id, previous] : impl.data->ids)
                    if (const auto current = selected.ids.find(id);
                        current != selected.ids.end() && current->second != previous)
                    {
                        changes.RecordObject(*owner_->Read().Object(previous), SceneChangeKind::Removed);
                        changes.RecordObject(*Read().Object(current->second), SceneChangeKind::Added);
                    }
                for (const auto& [key, bindings] : impl.data->bindings)
                    if (const auto current = selected.bindings.find(key); current != selected.bindings.end())
                        for (const auto& before : bindings)
                            if (::Alive(selected, impl.world, before.handle.entity))
                                for (const auto& after : current->second)
                                    if (before.id == after.id && before.handle != after.handle)
                                    {
                                        const auto id = *Read().ObjectId(after.handle.entity);
                                        const auto type = ComponentNames[static_cast<std::uint32_t>(key)];
                                        changes.components.push_back({id, before.id, type, SceneChangeKind::Removed});
                                        changes.components.push_back({id, after.id, type, SceneChangeKind::Added});
                                        changes.structureChanged = true;
                                    }
            }
            else
            {
                const auto& data = *impl.data;
                for (const auto& [slot, transform] : pending_->transforms)
                {
                    const auto& before = data.slots[slot].transform;
                    unsigned int fields = 0;
                    if (before.position != transform.position)
                        fields |= static_cast<unsigned int>(TransformField::Position);
                    if (before.rotation != transform.rotation)
                        fields |= static_cast<unsigned int>(TransformField::Rotation);
                    if (before.scale != transform.scale)
                        fields |= static_cast<unsigned int>(TransformField::Scale);
                    if (fields)
                        changes.transforms.push_back(
                            {data.definition->Source().objects[data.slots[slot].definitionIndex].id,
                             static_cast<TransformField>(fields)});
                }
                for (const auto& [key, value] : pending_->components)
                {
                    const auto empty = std::visit(
                        [](const auto& component) -> RuntimeComponentValue
                        {
                            return std::decay_t<decltype(component)>{};
                        },
                        value);
                    ScenePlacement before, after;
                    before.id = after.id = data.definition->Source().objects[data.slots[key.slot].definitionIndex].id;
                    std::visit(
                        [&](const auto& component)
                        {
                            Attach(before, component);
                        },
                        ::Value(data, key.slot, empty));
                    std::visit(
                        [&](const auto& component)
                        {
                            Attach(after, component);
                        },
                        value);
                    changes.RecordObject(before, after);
                }
            }
            pending_->delta.Swap(changes);
            if (!pending_->delta.Empty())
            {
                auto publication = impl.changes;
                publication.Merge(pending_->delta);
                pending_->publication = std::move(publication);
            }
            else
                pending_->publication.reset();
            pending_->prepared = true;
            error.clear();
            return true;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }

    void WorldState::SetDefinitionPreparation(std::function<void(SceneLayout&)> prepare)
    {
        if (impl_->active)
            throw std::logic_error("Cannot replace active definition preparation");
        impl_->prepareDefinition = std::move(prepare);
    }

    const SceneChangeSet& WorldState::Transaction::Changes() const noexcept
    {
        return pending_->delta;
    }
    bool WorldState::Transaction::Commit() noexcept
    {
        auto& impl = *owner_->impl_;
        if (impl.active != pending_.get() || !pending_->prepared)
            return false;
        auto& data = pending_->structure ? *pending_->structure : *impl.data;
        for (const auto& [slot, transform] : pending_->transforms)
            data.slots[slot].transform = transform;
        CommitPages(data, pending_->pages);
        if (pending_->structure)
            impl.data.swap(pending_->structure);
        if (pending_->matricesReady)
            impl.matrices.swap(pending_->matrices);
        if (pending_->publication)
            impl.changes.Swap(*pending_->publication);
        impl.nextId = pending_->nextId;
        impl.active = nullptr;
        return true;
    }
}
