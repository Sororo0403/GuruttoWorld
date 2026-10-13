#include "SceneComponentJson.h"
#include <Engine/Core/Validation.h>
#include "RagdollJson.h"
#include "EnvironmentJson.h"
#include "UiJson.h"
#include "GenreJson.h"
#include "AnimatorJson.h"
#include <SceneRuntime/MaterialAsset.h>
#include <SceneRuntime/SceneUi.h>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace
{
    using Engine::Json;
    using Engine::JsonArray;
    using Engine::JsonObject;
    using Engine::JsonNumber;
    SceneRuntime::ScriptValue ReadScriptValue(const Json& source,size_t depth,size_t& remaining)
    {
        if (depth>8 || remaining==0) throw std::runtime_error("Script data exceeds nesting or element limit");
        --remaining;
        const auto& object=JsonObject(source);
        if (object.size()!=2) throw std::runtime_error("Invalid script data encoding");
        const auto kind=object.at("kind").get<std::string>();
        const auto& value=object.at("value");
        using Value=SceneRuntime::ScriptValue;
        if (kind=="number") {
            const auto number=JsonNumber(value);
            if (!Engine::InRange(number,-1000000,1000000)) throw std::runtime_error("Invalid script data number");
            return {static_cast<float>(number)};
        }
        if (kind=="bool") return {value.get<bool>()};
        if (kind=="string") return {value.get<std::string>()};
        if (kind=="reference") return {SceneRuntime::ScriptObjectReference{value.get<std::string>()}};
        if (kind=="array") {
            Value::Array result;
            const auto& children=JsonArray(value);
            std::transform(children.begin(),children.end(),std::back_inserter(result),[&](const auto& child) {return ReadScriptValue(child,depth+1,remaining);});
            return {std::move(result)};
        }
        if (kind=="object") {
            Value::Object result;
            for (const auto& [key,child]:JsonObject(value).items()) result.emplace(key,ReadScriptValue(child,depth+1,remaining));
            return {std::move(result)};
        }
        throw std::runtime_error("Unknown script data kind");
    }
    Json WriteScriptValue(const SceneRuntime::ScriptValue& source)
    {
        return std::visit([](const auto& value)->Json {
            using T=std::decay_t<decltype(value)>;
            using Value=SceneRuntime::ScriptValue;
            if constexpr (std::is_same_v<T,float>) return {{"kind","number"},{"value",value}};
            else if constexpr (std::is_same_v<T,bool>) return {{"kind","bool"},{"value",value}};
            else if constexpr (std::is_same_v<T,std::string>) return {{"kind","string"},{"value",value}};
            else if constexpr (std::is_same_v<T,SceneRuntime::ScriptObjectReference>) return {{"kind","reference"},{"value",value.id}};
            else if constexpr (std::is_same_v<T,Value::Array>) {
                Json array=Json::array();
                std::transform(value.begin(),value.end(),std::back_inserter(array),[](const auto& child) {return WriteScriptValue(child);});
                return {{"kind","array"},{"value",std::move(array)}};
            } else {
                Json object=Json::object();
                for (const auto& [key,child]:value) object[key]=WriteScriptValue(child);
                return {{"kind","object"},{"value",std::move(object)}};
            }
        },source.value);
    }
    std::filesystem::path ReadModel(const Json& object)
    {
        const auto text=object.at("model").get<std::string>();
        const auto path=std::filesystem::path(std::u8string(text.begin(),text.end()));
        auto extension=path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (path.is_absolute() || path.has_root_name() || !text.starts_with("Assets/Models/") ||
            (extension!=".obj" && extension!=".gltf" && extension!=".glb") || std::any_of(path.begin(),path.end(),[](const auto& part) { return part==".."; }))
            throw std::runtime_error("MeshRenderer model must be OBJ, glTF or GLB relative to Assets/Models");
        return path;
    }

    std::array<float,3> ReadVelocity(const Json& object)
    {
        const auto& array=JsonArray(object.at("angularVelocity"));
        if (array.size()!=3) throw std::runtime_error("Rotator angularVelocity requires three components");
        std::array<float,3> result;
        for (uint32_t index=0;index<3;++index)
        {
            const auto value=JsonNumber(array.at(index));
            if (!Engine::InRange(value,-100000.0,100000.0)) throw std::runtime_error("Rotator speed must be finite and within +/-100000 degrees/s");
            result[index]=static_cast<float>(value);
        }
        return result;
    }

    Json Component(const std::string& id, const char* type, bool enabled)
    {
        Json object=Json::object();
        object["id"]=id;
        object["type"]=type;
        object["enabled"]=enabled;
        return object;
    }
    SceneRuntime::AnimationComponent ReadAnimation(const Json& object)
    {
        SceneRuntime::AnimationComponent animation;
        animation.id=object.at("id").get<std::string>();
        animation.enabled=object.at("enabled").get<bool>();
        for (const auto& value : JsonArray(object.at("tracks")))
        {
            SceneRuntime::AnimationTrack track;
            track.property=value.at("property").get<std::string>();
            track.clock=value.at("clock").get<std::string>();
            track.easing=value.at("easing").get<std::string>();
            track.delay=static_cast<float>(JsonNumber(value.at("delay")));
            track.loop=value.at("loop").get<bool>();
            track.keys.clear();
            for (const auto& key : JsonArray(value.at("keys")))
            {
                SceneRuntime::AnimationKey frame;
                frame.time=static_cast<float>(JsonNumber(key.at("time")));
                const auto& vector=JsonArray(key.at("value"));
                if (vector.size()!=4) throw std::runtime_error("Animation value requires four numbers");
                for (size_t axis=0;axis<4;++axis) frame.value[axis]=static_cast<float>(JsonNumber(vector.at(axis)));
                track.keys.push_back(frame);
            }
            if (!SceneRuntime::Animation::Valid(track)) throw std::runtime_error("Invalid animation track");
            animation.tracks.push_back(std::move(track));
        }
        if (animation.tracks.size()>32) throw std::runtime_error("Too many animation tracks");
        return animation;
    }
    Json WriteAnimation(const SceneRuntime::AnimationComponent& animation)
    {
        auto object=Component(animation.id,"Animation",animation.enabled);
        object["tracks"]=Json::array();
        for (const auto& track : animation.tracks)
        {
            Json keys=Json::array();
            std::transform(track.keys.begin(),track.keys.end(),std::back_inserter(keys),[](const auto& key) {
                return Json{{"time",key.time},{"value",key.value}};
            });
            object["tracks"].push_back({{"property",track.property},{"clock",track.clock},
                {"easing",track.easing},{"delay",track.delay},{"loop",track.loop},{"keys",keys}});
        }
        return object;
    }
    void ReadLegacyMaterial(const Json &component, const std::string &type, std::filesystem::path &legacyMaterial)
    {
        if (type == "MeshRenderer" && component.contains("material")) {
            const auto text = component.at("material").get<std::string>();
            legacyMaterial = std::filesystem::path(std::u8string(text.begin(), text.end()));
            if (!legacyMaterial.empty() && !SceneRuntime::MaterialAsset::ValidPath(legacyMaterial)) throw std::runtime_error("Invalid legacy mesh material path");
        }
    }

    struct ComponentSource {
        ComponentSource(const Json &value, std::string identifier, bool active) : component(value), id(std::move(identifier)), enabled(active) {}
        const Json& component;std::string id;bool enabled;
    };

    void ReadPlacementMeshRenderer(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.meshRenderer) throw std::runtime_error("Only one MeshRenderer is allowed");
        placement.meshRenderer = SceneRuntime::MeshRendererComponent{id, enabled, ReadModel(component)};
        if (component.contains("visibleWhen")) placement.meshRenderer->visibleWhen = component.at("visibleWhen").get<std::string>();
        auto &mesh = *placement.meshRenderer;
        if (component.contains("instancing")) mesh.instancing = component.at("instancing").get<bool>();
        if (component.contains("occlusionCulling")) mesh.occlusionCulling = component.at("occlusionCulling").get<bool>();
        if (component.contains("lods"))
        {
            const auto &levels = JsonArray(component.at("lods"));
            if (levels.size() > 8) throw std::runtime_error("Mesh LOD count exceeds eight");
            float previous = 0;
            for (const auto &level : levels)
            {
                const auto distance = JsonNumber(level.at("distance"));
                if (!std::isfinite(distance) || distance <= previous || distance > 100000) throw std::runtime_error("LOD distances must increase within (0,100000]");
                mesh.lods.push_back({static_cast<float>(distance), ReadModel(level)}); previous = static_cast<float>(distance);
            }
        }
        SceneRuntime::UiState check;
        if (!check.Assign(placement.meshRenderer->visibleWhen)) throw std::runtime_error("Invalid mesh visibility expression");

    }

    void ReadPlacementMaterial(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.material) throw std::runtime_error("Only one Material is allowed");
        const auto text = component.at("asset").get<std::string>();
        const auto path = std::filesystem::path(std::u8string(text.begin(), text.end()));
        if (!path.empty() && !SceneRuntime::MaterialAsset::ValidPath(path)) throw std::runtime_error("Invalid material asset path");
        placement.material = SceneRuntime::MaterialComponent{id, enabled, path};
        auto &material = *placement.material;
        material.surfaceOverride = component.value("surfaceOverride", false);
        if (component.contains("color")) material.color = component.at("color").get<std::array<float, 4>>();
        material.roughness = component.value("roughness", .5f);material.metallic = component.value("metallic", 0.0f);
        if (std::any_of(std::begin(material.color), std::end(material.color), [](const auto channel) { return !Engine::InRange(channel, 0, 1); })) throw std::runtime_error("Invalid material override color");
        if (!Engine::InRange(material.roughness, .04f, 1) || !Engine::InRange(material.metallic, 0, 1)) throw std::runtime_error("Invalid material override surface");
        if (component.contains("slots")) {
            const auto &slots = Engine::JsonArray(component.at("slots"));
            if (slots.size() > 4096) throw std::runtime_error("Too many Material slots");
            for (const auto &value : slots) {
                const auto slotText = value.get<std::string>();const auto slot = std::filesystem::path(std::u8string(slotText.begin(), slotText.end()));
                if (!slot.empty() && !SceneRuntime::MaterialAsset::ValidPath(slot)) throw std::runtime_error("Invalid Material slot asset path");
                placement.material->slots.push_back(slot);
            }
        }

    }

    void ReadPlacementRotator(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.rotator) throw std::runtime_error("Only one Rotator is allowed");
        placement.rotator = SceneRuntime::RotatorComponent{id, enabled, ReadVelocity(component)};

    }

    void ReadPlacementPlayerController(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.playerController) throw std::runtime_error("Only one PlayerController is allowed");
        const double speed = JsonNumber(component.at("moveSpeed"));
        if (!Engine::InRange(speed, 0, 1000)) throw std::runtime_error("Invalid PlayerController speed");
        placement.playerController = SceneRuntime::PlayerControllerComponent{id, enabled, static_cast<float>(speed)};
        auto &player = *placement.playerController;
        if (component.contains("useGravity")) player.useGravity = component.at("useGravity").get<bool>();
        if (component.contains("gravity")) player.gravity = static_cast<float>(JsonNumber(component.at("gravity")));
        if (component.contains("jumpSpeed")) player.jumpSpeed = static_cast<float>(JsonNumber(component.at("jumpSpeed")));
        player.usePhysics = component.value("usePhysics", false);
        if (component.contains("maxSlopeDegrees")) player.maxSlopeDegrees = static_cast<float>(JsonNumber(component.at("maxSlopeDegrees")));
        if (component.contains("stepHeight")) player.stepHeight = static_cast<float>(JsonNumber(component.at("stepHeight")));
        if (!Engine::InRange(player.gravity, 0, 1000) ||
            !Engine::InRange(player.jumpSpeed, 0, 1000) ||
            !Engine::InRange(player.maxSlopeDegrees, 0, 89) ||
            !Engine::InRange(player.stepHeight, 0, 10)) throw std::runtime_error("Invalid player gravity, jump or slope settings");

    }

    void ReadColliderBounds(const Json &component, SceneRuntime::BoxColliderComponent &collider)
    {
        for (const auto *key : {"center", "size"})
        {
            const auto &values = JsonArray(component.at(key));
            if (values.size() != 3) throw std::runtime_error("BoxCollider requires three components");
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double number = JsonNumber(values.at(axis));
                if (!Engine::InRange(number, -100000, 100000) || (std::string_view(key) == "size" && number < 0.001))
                    throw std::runtime_error("Invalid BoxCollider bounds");
                auto &bounds = std::string_view(key) == "size" ? collider.size : collider.center;
                bounds[axis] = static_cast<float>(number);
            }
        }
    }

    void ReadCollisionFilter(const Json &component, SceneRuntime::BoxColliderComponent &collider)
    {
        for (const auto *key : {"layer", "mask"}) if (component.contains(key))
            {
                const auto &value = component.at(key);
                if (!value.is_number_integer() || JsonNumber(value) < 0 || JsonNumber(value) > 4294967295.0) throw std::runtime_error("Invalid collision layer or mask");
                (std::string_view(key) == "layer" ? collider.layer : collider.mask) = value.get<unsigned int>();
            }
    }

    void ReadPlacementBoxCollider(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.boxCollider) throw std::runtime_error("Only one BoxCollider is allowed");
        SceneRuntime::BoxColliderComponent collider; collider.id = id; collider.enabled = enabled;
        ReadColliderBounds(component, collider);
        collider.shape = component.value("shape", std::string("box"));
        collider.isTrigger = component.value("isTrigger", false);
        collider.convex = component.value("convex", false);
        if (component.contains("radius")) collider.radius = static_cast<float>(JsonNumber(component.at("radius")));
        if (component.contains("halfHeight")) collider.halfHeight = static_cast<float>(JsonNumber(component.at("halfHeight")));
        ReadCollisionFilter(component, collider);
        if (!Engine::OneOf(collider.shape, {"box", "sphere", "capsule", "mesh", "terrain", "tilemap"})) throw std::runtime_error("Invalid collider shape");
        if (collider.layer > 31 || !Engine::InRange(collider.radius, 0.001f, 100000) ||
            !Engine::InRange(collider.halfHeight, 0, 100000)) throw std::runtime_error("Invalid collider radius, height or layer");
        if (component.contains("model") && !component.at("model").get<std::string>().empty()) collider.model = ReadModel(component);
        placement.boxCollider = collider;

    }

    void ReadPlacementRigidBody(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.rigidBody) throw std::runtime_error("Only one RigidBody is allowed");
        SceneRuntime::RigidBodyComponent body; body.id = id; body.enabled = enabled;
        body.motion = component.value("motion", std::string("dynamic")); body.continuous = component.value("continuous", true);
        body.planar = component.value("planar", false);
        const std::pair<const char *, float *> fields[] = {{"mass", &body.mass}, {"friction", &body.friction}, {"restitution", &body.restitution},
            {"gravityScale", &body.gravityScale}, {"linearDamping", &body.linearDamping}, {"angularDamping", &body.angularDamping}};
        for (const auto &[key, target] : fields) if (component.contains(key)) *target = static_cast<float>(JsonNumber(component.at(key)));
        for (const auto *key : {"velocity", "angularVelocity"}) if (component.contains(key))
            {
                const auto values = component.at(key).get<std::array<float, 3>>();
                if (std::any_of(std::begin(values), std::end(values), [](const auto value) { return !Engine::InRange(value, -100000, 100000); })) throw std::runtime_error("Invalid rigid body velocity");
                (std::string_view(key) == "velocity" ? body.velocity : body.angularVelocity) = values;
            }
        if (!Engine::OneOf(body.motion, {"dynamic", "kinematic"}) || !Engine::PositiveRange(body.mass, 100000) ||
            !Engine::InRange(body.friction, 0, 1) || !Engine::InRange(body.restitution, 0, 1) ||
            !Engine::InRange(body.gravityScale, 0, 10) || !Engine::InRange(body.linearDamping, 0, 10) ||
            !Engine::InRange(body.angularDamping, 0, 10)) throw std::runtime_error("Invalid rigid body properties");
        placement.rigidBody = body;

    }

    void ReadPlacementRagdoll(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        if (placement.ragdoll) throw std::runtime_error("Only one Ragdoll is allowed");
        auto ragdoll = component.get<SceneRuntime::RagdollComponent>(); ragdoll.Validate(); placement.ragdoll = std::move(ragdoll);

    }

    void ReadPlacementJoint(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.joint) throw std::runtime_error("Only one Joint is allowed");
        SceneRuntime::JointComponent joint; joint.id = id; joint.enabled = enabled;
        joint.type = component.value("kind", std::string("fixed")); joint.target = component.value("target", std::string{});
        joint.collideConnected = component.value("collideConnected", false);
        joint.anchor = component.value("anchor", joint.anchor);joint.connectedAnchor = component.value("connectedAnchor", joint.connectedAnchor);
        joint.axis = component.value("axis", joint.axis);joint.minimum = component.value("minimum", joint.minimum);joint.maximum = component.value("maximum", joint.maximum);
        joint.swing = component.value("swing", joint.swing);joint.minDistance = component.value("minDistance", joint.minDistance);joint.maxDistance = component.value("maxDistance", joint.maxDistance);
        joint.Validate();placement.joint = joint;

    }

    void ReadPlacementScript(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.scripts.size() >= 32) throw std::runtime_error("Too many scripts");
        SceneRuntime::ScriptComponent script; script.id = id; script.enabled = enabled;
        script.behaviour = component.at("behaviour").get<std::string>();
        if (script.behaviour.empty() || script.behaviour.size() > 128 || script.behaviour.find('\0') != std::string::npos) throw std::runtime_error("Invalid script behaviour");
        for (const auto &[key, value] : JsonObject(component.at("parameters")).items())
        {
            const double number = JsonNumber(value);
            if (key.empty() || key.size() > 128 || key.find('\0') != std::string::npos || !Engine::InRange(number, -1000000, 1000000))
                throw std::runtime_error("Invalid script parameter");
            script.parameters[key] = static_cast<float>(number);
        }
        if (script.parameters.size() > 64) throw std::runtime_error("Too many script parameters");
        if (component.contains("data")) {
            size_t remaining = 4096;
            auto data = ReadScriptValue(component.at("data"), 0, remaining);
            remaining = 4096; data.Validate(0, remaining);
            if (!std::holds_alternative<SceneRuntime::ScriptValue::Object>(data.value)) throw std::runtime_error("Script data must be an object");
            script.data = std::get<SceneRuntime::ScriptValue::Object>(std::move(data.value));
        }
        placement.scripts.push_back(std::move(script));

    }

    void ReadPlacementAnimator(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        const auto &id = source.id;
        const auto &enabled = source.enabled;
        if (placement.animator) throw std::runtime_error("Only one Animator is allowed");
        placement.animator = SceneRuntime::ReadAnimator(component, id, enabled);

    }

    void ReadPlacementAnimation(const ComponentSource &source, SceneRuntime::ScenePlacement &placement)
    {
        const auto &component = source.component;
        if (placement.animation) throw std::runtime_error("Only one Animation is allowed");
        placement.animation = ReadAnimation(component);

    }

    bool ReadBasicComponent(const Json& component,SceneRuntime::ScenePlacement& placement,const std::string& type)
    {
        const ComponentSource source{component,component.at("id").get<std::string>(),component.at("enabled").get<bool>()};
        using Reader=void(*)(const ComponentSource&,SceneRuntime::ScenePlacement&);
        static const std::pair<std::string_view,Reader> readers[]{
            {"MeshRenderer",ReadPlacementMeshRenderer},
            {"Material",ReadPlacementMaterial},
            {"Rotator",ReadPlacementRotator},
            {"PlayerController",ReadPlacementPlayerController},
            {"BoxCollider",ReadPlacementBoxCollider},
            {"RigidBody",ReadPlacementRigidBody},
            {"Ragdoll",ReadPlacementRagdoll},
            {"Joint",ReadPlacementJoint},
            {"Script",ReadPlacementScript},
            {"Animator",ReadPlacementAnimator},
            {"Animation",ReadPlacementAnimation},
        };
        const auto found=std::find_if(std::begin(readers),std::end(readers),[&](const auto& reader) {return reader.first==type;});
        if(found==std::end(readers)) return false;
        found->second(source,placement);
        return true;
    }
    void AppendJoint(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.joint)
        {
            const auto &joint = *placement.joint; joint.Validate(); auto object = Component(joint.id, "Joint", joint.enabled);
            object["kind"] = joint.type;object["target"] = joint.target;object["collideConnected"] = joint.collideConnected;
            object["anchor"] = joint.anchor;object["connectedAnchor"] = joint.connectedAnchor;object["axis"] = joint.axis;
            object["minimum"] = joint.minimum;object["maximum"] = joint.maximum;object["swing"] = joint.swing;
            object["minDistance"] = joint.minDistance;object["maxDistance"] = joint.maxDistance;components.push_back(std::move(object));
        }
    }

    void AppendMeshRenderer(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.meshRenderer)
        {
            const auto &mesh = *placement.meshRenderer;
            auto object = Component(mesh.id, "MeshRenderer", mesh.enabled);
            object["model"] = mesh.model;
            object["visibleWhen"] = mesh.visibleWhen;
            object["instancing"] = mesh.instancing; object["occlusionCulling"] = mesh.occlusionCulling;
            object["lods"] = Json::array();
            for (const auto &lod : mesh.lods) object["lods"].push_back({{"distance", lod.distance}, {"model", lod.model}});
            components.push_back(object);
        }
    }

    void AppendMaterial(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.material)
        {
            const auto &material = *placement.material;
            auto object = Component(material.id, "Material", material.enabled);
            object["asset"] = material.asset;
            object["surfaceOverride"] = material.surfaceOverride;object["color"] = material.color;
            object["roughness"] = material.roughness;object["metallic"] = material.metallic;
            if (!material.slots.empty()) object["slots"] = material.slots;
            components.push_back(std::move(object));
        }
    }

    void AppendRotator(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.rotator)
        {
            const auto &rotator = *placement.rotator;
            auto object = Component(rotator.id, "Rotator", rotator.enabled);
            Json velocity = Json::array();
            for (const auto value : rotator.angularVelocity)
            {
                if (!Engine::InRange(value, -100000.0f, 100000.0f)) throw std::runtime_error("Invalid Rotator speed");
                velocity.push_back(value);
            }
            object["angularVelocity"] = velocity;
            components.push_back(object);
        }
    }

    void AppendPlayerController(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.playerController)
        {
            const auto &player = *placement.playerController;
            if (!Engine::InRange(player.moveSpeed, 0, 1000)) throw std::runtime_error("Invalid PlayerController speed");
            auto object = Component(player.id, "PlayerController", player.enabled);
            if (!Engine::InRange(player.gravity, 0, 1000) ||
                !Engine::InRange(player.jumpSpeed, 0, 1000)) throw std::runtime_error("Invalid player gravity or jump speed");
            object["moveSpeed"] = player.moveSpeed;
            object["useGravity"] = player.useGravity; object["gravity"] = player.gravity; object["jumpSpeed"] = player.jumpSpeed;
            object["usePhysics"] = player.usePhysics; object["maxSlopeDegrees"] = player.maxSlopeDegrees; object["stepHeight"] = player.stepHeight;
            components.push_back(object);
        }
    }

    void AppendBoxCollider(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.boxCollider)
        {
            const auto &collider = *placement.boxCollider;
            for (size_t axis = 0; axis < 3; ++axis)
                if (!Engine::InRange(collider.center[axis], -100000, 100000) ||
                    !Engine::InRange(collider.size[axis], 0.001f, 100000))
                    throw std::runtime_error("Invalid BoxCollider bounds");
            auto object = Component(collider.id, "BoxCollider", collider.enabled);
            object["center"] = collider.center; object["size"] = collider.size;
            object["shape"] = collider.shape; object["radius"] = collider.radius; object["halfHeight"] = collider.halfHeight;
            object["isTrigger"] = collider.isTrigger; object["layer"] = collider.layer; object["mask"] = collider.mask; object["model"] = collider.model;
            object["convex"] = collider.convex;
            components.push_back(object);
        }
    }

    void AppendRigidBody(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        if (placement.rigidBody)
        {
            const auto &body = *placement.rigidBody;
            auto object = Component(body.id, "RigidBody", body.enabled);
            object["motion"] = body.motion; object["continuous"] = body.continuous;
            object["planar"] = body.planar;
            object["mass"] = body.mass; object["friction"] = body.friction; object["restitution"] = body.restitution;
            object["gravityScale"] = body.gravityScale; object["linearDamping"] = body.linearDamping; object["angularDamping"] = body.angularDamping;
            object["velocity"] = body.velocity; object["angularVelocity"] = body.angularVelocity;
            components.push_back(object);
        }
    }

    void AppendScripts(Json &components, const SceneRuntime::ScenePlacement &placement)
    {
        for (const auto &script : placement.scripts)
        {
            auto object = Component(script.id, "Script", script.enabled);
            object["behaviour"] = script.behaviour; object["parameters"] = script.parameters;
            if (!script.data.empty()) {
                SceneRuntime::ScriptValue data{script.data}; size_t remaining = 4096; data.Validate(0, remaining);
                object["data"] = WriteScriptValue(data);
            }
            components.push_back(object);
        }
    }

}

namespace SceneRuntime
{
    void ReadSceneComponents(const Json& object, ScenePlacement& placement, bool legacy)
    {
        if (legacy)
        {
            if (object.contains("components")) throw std::runtime_error("Version 2 cannot contain components");
            placement.SetModel(ReadModel(object));
            return;
        }
        if (object.contains("model")) throw std::runtime_error("Version 3 model belongs to MeshRenderer");
        std::unordered_set<std::string> ids{"transform"};
        std::filesystem::path legacyMaterial;
        for (const auto& value : JsonArray(object.at("components")))
        {
            const auto& component=JsonObject(value);
            const auto id=component.at("id").get<std::string>();
            if (id.empty() || id.find('\0')!=std::string::npos || !ids.insert(id).second)
                throw std::runtime_error("Empty, duplicate or reserved component ID");
            const auto type=component.at("type").get<std::string>();
            ReadLegacyMaterial(component,type,legacyMaterial);
            if (!ReadBasicComponent(component,placement,type) && !ReadEnvironmentComponent(component,placement,type) &&
                !(ReadGenreComponent(component,placement,type) || ReadUiComponent(component,placement,type)))
                throw std::runtime_error("Unsupported component type: "+type);
        }
        if (!legacyMaterial.empty()) {
            if (placement.material) throw std::runtime_error("Material specified both in MeshRenderer and Material component");
            std::string id="material"; size_t number=2;
            while (ids.contains(id)) id="material-"+std::to_string(number++);
            placement.material=MaterialComponent{id,true,legacyMaterial};
        }
    }

    Json WriteSceneComponents(const ScenePlacement& placement)
    {
        Json components=Json::array();
        AppendJoint(components,placement);
        AppendMeshRenderer(components,placement);
        AppendMaterial(components,placement);
        AppendRotator(components,placement);
        AppendPlayerController(components,placement);
        AppendBoxCollider(components,placement);
        AppendRigidBody(components,placement);
        AppendScripts(components,placement);
        if (placement.animator) components.push_back(WriteAnimator(*placement.animator));
        if(placement.ragdoll) {placement.ragdoll->Validate(); Json object=*placement.ragdoll; object["type"]="Ragdoll"; components.push_back(std::move(object));}
        WriteEnvironmentComponents(components,placement);
        WriteUiComponents(components,placement);
        WriteGenreComponents(components,placement);
        if (placement.animation) components.push_back(WriteAnimation(*placement.animation));
        return components;
    }
}
