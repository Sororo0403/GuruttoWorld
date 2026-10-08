#include "SceneComponentJson.h"
#include "EnvironmentJson.h"
#include "UiJson.h"
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
            if (!std::isfinite(value) || std::abs(value)>100000.0) throw std::runtime_error("Rotator speed must be finite and within +/-100000 degrees/s");
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
    bool ReadBasicComponent(const Json& component,SceneRuntime::ScenePlacement& placement,const std::string& type)
    {
        const auto id=component.at("id").get<std::string>();
        const bool enabled=component.at("enabled").get<bool>();
        if (type=="MeshRenderer")
        {
            if (placement.meshRenderer) throw std::runtime_error("Only one MeshRenderer is allowed");
            placement.meshRenderer=SceneRuntime::MeshRendererComponent{id,enabled,ReadModel(component)};
            if (component.contains("visibleWhen")) placement.meshRenderer->visibleWhen=component.at("visibleWhen").get<std::string>();
            if (component.contains("material"))
            {
                const auto text=component.at("material").get<std::string>();
                placement.meshRenderer->material=std::filesystem::path(std::u8string(text.begin(),text.end()));
                if (!placement.meshRenderer->material.empty() && !SceneRuntime::MaterialAsset::ValidPath(placement.meshRenderer->material)) throw std::runtime_error("Invalid mesh material path");
            }
            SceneRuntime::UiState check;
            if (!check.Assign(placement.meshRenderer->visibleWhen)) throw std::runtime_error("Invalid mesh visibility expression");
        }
        else if (type=="Rotator")
        {
            if (placement.rotator) throw std::runtime_error("Only one Rotator is allowed");
            placement.rotator=SceneRuntime::RotatorComponent{id,enabled,ReadVelocity(component)};
        }
        else if (type=="PlayerController")
        {
            if (placement.playerController) throw std::runtime_error("Only one PlayerController is allowed");
            const double speed=JsonNumber(component.at("moveSpeed"));
            if (!std::isfinite(speed) || speed<0 || speed>1000) throw std::runtime_error("Invalid PlayerController speed");
            placement.playerController=SceneRuntime::PlayerControllerComponent{id,enabled,static_cast<float>(speed)};
            auto& player=*placement.playerController;
            if (component.contains("useGravity")) player.useGravity=component.at("useGravity").get<bool>();
            if (component.contains("gravity")) player.gravity=static_cast<float>(JsonNumber(component.at("gravity")));
            if (component.contains("jumpSpeed")) player.jumpSpeed=static_cast<float>(JsonNumber(component.at("jumpSpeed")));
            player.usePhysics=component.value("usePhysics",false);
            if (component.contains("maxSlopeDegrees")) player.maxSlopeDegrees=static_cast<float>(JsonNumber(component.at("maxSlopeDegrees")));
            if (component.contains("stepHeight")) player.stepHeight=static_cast<float>(JsonNumber(component.at("stepHeight")));
            if (!std::isfinite(player.gravity) || player.gravity<0 || player.gravity>1000 ||
                !std::isfinite(player.jumpSpeed) || player.jumpSpeed<0 || player.jumpSpeed>1000 ||
                !std::isfinite(player.maxSlopeDegrees) || player.maxSlopeDegrees<0 || player.maxSlopeDegrees>89 ||
                !std::isfinite(player.stepHeight) || player.stepHeight<0 || player.stepHeight>10) throw std::runtime_error("Invalid player gravity, jump or slope settings");
        }
        else if (type=="BoxCollider")
        {
            if (placement.boxCollider) throw std::runtime_error("Only one BoxCollider is allowed");
            SceneRuntime::BoxColliderComponent collider; collider.id=id; collider.enabled=enabled;
            for (const auto* key : {"center","size"})
            {
                const auto& values=JsonArray(component.at(key));
                if (values.size()!=3) throw std::runtime_error("BoxCollider requires three components");
                for (size_t axis=0;axis<3;++axis)
                {
                    const double number=JsonNumber(values.at(axis));
                    if (!std::isfinite(number) || std::abs(number)>100000 || (std::string_view(key)=="size" && number<0.001))
                        throw std::runtime_error("Invalid BoxCollider bounds");
                    (std::string_view(key)=="size" ? collider.size : collider.center)[axis]=static_cast<float>(number);
                }
            }
            collider.shape=component.value("shape",std::string("box"));
            collider.isTrigger=component.value("isTrigger",false);
            collider.convex=component.value("convex",false);
            if (component.contains("radius")) collider.radius=static_cast<float>(JsonNumber(component.at("radius")));
            if (component.contains("halfHeight")) collider.halfHeight=static_cast<float>(JsonNumber(component.at("halfHeight")));
            for (const auto* key : {"layer","mask"}) if (component.contains(key))
            {
                const auto& value=component.at(key);
                if (!value.is_number_integer() || JsonNumber(value)<0 || JsonNumber(value)>4294967295.0) throw std::runtime_error("Invalid collision layer or mask");
                (std::string_view(key)=="layer" ? collider.layer : collider.mask)=value.get<unsigned int>();
            }
            if (collider.shape!="box" && collider.shape!="sphere" && collider.shape!="capsule" && collider.shape!="mesh") throw std::runtime_error("Invalid collider shape");
            if (collider.layer>31 || !std::isfinite(collider.radius) || collider.radius<0.001f || collider.radius>100000 ||
                !std::isfinite(collider.halfHeight) || collider.halfHeight<0 || collider.halfHeight>100000) throw std::runtime_error("Invalid collider radius, height or layer");
            if (component.contains("model") && !component.at("model").get<std::string>().empty()) collider.model=ReadModel(component);
            placement.boxCollider=collider;
        }
        else if (type=="RigidBody")
        {
            if (placement.rigidBody) throw std::runtime_error("Only one RigidBody is allowed");
            SceneRuntime::RigidBodyComponent body; body.id=id; body.enabled=enabled;
            body.motion=component.value("motion",std::string("dynamic")); body.continuous=component.value("continuous",true);
            const std::pair<const char*,float*> fields[]={{"mass",&body.mass},{"friction",&body.friction},{"restitution",&body.restitution},
                {"gravityScale",&body.gravityScale},{"linearDamping",&body.linearDamping},{"angularDamping",&body.angularDamping}};
            for (const auto& [key,target] : fields) if (component.contains(key)) *target=static_cast<float>(JsonNumber(component.at(key)));
            for (const auto* key : {"velocity","angularVelocity"}) if (component.contains(key))
            {
                const auto values=component.at(key).get<std::array<float,3>>();
                for (const auto value : values) if (!std::isfinite(value) || std::abs(value)>100000) throw std::runtime_error("Invalid rigid body velocity");
                (std::string_view(key)=="velocity" ? body.velocity : body.angularVelocity)=values;
            }
            if ((body.motion!="dynamic" && body.motion!="kinematic") || !std::isfinite(body.mass) || body.mass<=0 || body.mass>100000 ||
                !std::isfinite(body.friction) || body.friction<0 || body.friction>1 || !std::isfinite(body.restitution) || body.restitution<0 || body.restitution>1 ||
                !std::isfinite(body.gravityScale) || body.gravityScale<0 || body.gravityScale>10 || !std::isfinite(body.linearDamping) || body.linearDamping<0 || body.linearDamping>10 ||
                !std::isfinite(body.angularDamping) || body.angularDamping<0 || body.angularDamping>10) throw std::runtime_error("Invalid rigid body properties");
            placement.rigidBody=body;
        }
        else if (type=="Script")
        {
            if (placement.scripts.size()>=32) throw std::runtime_error("Too many scripts");
            SceneRuntime::ScriptComponent script; script.id=id; script.enabled=enabled;
            script.behaviour=component.at("behaviour").get<std::string>();
            if (script.behaviour.empty() || script.behaviour.size()>128 || script.behaviour.find('\0')!=std::string::npos) throw std::runtime_error("Invalid script behaviour");
            for (const auto& [key,value] : JsonObject(component.at("parameters")).items())
            {
                const double number=JsonNumber(value);
                if (key.empty() || key.size()>128 || key.find('\0')!=std::string::npos || !std::isfinite(number) || std::abs(number)>1000000)
                    throw std::runtime_error("Invalid script parameter");
                script.parameters[key]=static_cast<float>(number);
            }
            if (script.parameters.size()>64) throw std::runtime_error("Too many script parameters");
            placement.scripts.push_back(std::move(script));
        }
        else if (type=="Animator")
        {
            if (placement.animator) throw std::runtime_error("Only one Animator is allowed");
            placement.animator=SceneRuntime::ReadAnimator(component,id,enabled);
        }
        else if (type=="Animation")
        {
            if (placement.animation) throw std::runtime_error("Only one Animation is allowed");
            placement.animation=ReadAnimation(component);
        }
        else return false;
        return true;
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
        for (const auto& value : JsonArray(object.at("components")))
        {
            const auto& component=JsonObject(value);
            const auto id=component.at("id").get<std::string>();
            if (id.empty() || id.find('\0')!=std::string::npos || !ids.insert(id).second)
                throw std::runtime_error("Empty, duplicate or reserved component ID");
            const auto type=component.at("type").get<std::string>();
            if (!ReadBasicComponent(component,placement,type) && !ReadEnvironmentComponent(component,placement,type) &&
                !ReadUiComponent(component,placement,type))
                throw std::runtime_error("Unsupported component type: "+type);
        }
    }

    Json WriteSceneComponents(const ScenePlacement& placement)
    {
        Json components=Json::array();
        if (placement.meshRenderer)
        {
            const auto& mesh=*placement.meshRenderer;
            auto object=Component(mesh.id,"MeshRenderer",mesh.enabled);
            object["model"]=mesh.model;
            object["visibleWhen"]=mesh.visibleWhen;
            object["material"]=mesh.material;
            components.push_back(object);
        }
        if (placement.rotator)
        {
            const auto& rotator=*placement.rotator;
            auto object=Component(rotator.id,"Rotator",rotator.enabled);
            Json velocity=Json::array();
            for (const auto value : rotator.angularVelocity)
            {
                if (!std::isfinite(value) || std::abs(value)>100000.0f) throw std::runtime_error("Invalid Rotator speed");
                velocity.push_back(value);
            }
            object["angularVelocity"]=velocity;
            components.push_back(object);
        }
        if (placement.playerController)
        {
            const auto& player=*placement.playerController;
            if (!std::isfinite(player.moveSpeed) || player.moveSpeed<0 || player.moveSpeed>1000) throw std::runtime_error("Invalid PlayerController speed");
            auto object=Component(player.id,"PlayerController",player.enabled);
            if (!std::isfinite(player.gravity) || player.gravity<0 || player.gravity>1000 ||
                !std::isfinite(player.jumpSpeed) || player.jumpSpeed<0 || player.jumpSpeed>1000) throw std::runtime_error("Invalid player gravity or jump speed");
            object["moveSpeed"]=player.moveSpeed;
            object["useGravity"]=player.useGravity; object["gravity"]=player.gravity; object["jumpSpeed"]=player.jumpSpeed;
            object["usePhysics"]=player.usePhysics; object["maxSlopeDegrees"]=player.maxSlopeDegrees; object["stepHeight"]=player.stepHeight;
            components.push_back(object);
        }
        if (placement.boxCollider)
        {
            const auto& collider=*placement.boxCollider;
            for (size_t axis=0;axis<3;++axis)
                if (!std::isfinite(collider.center[axis]) || std::abs(collider.center[axis])>100000 ||
                    !std::isfinite(collider.size[axis]) || collider.size[axis]<0.001f || collider.size[axis]>100000)
                    throw std::runtime_error("Invalid BoxCollider bounds");
            auto object=Component(collider.id,"BoxCollider",collider.enabled);
            object["center"]=collider.center; object["size"]=collider.size;
            object["shape"]=collider.shape; object["radius"]=collider.radius; object["halfHeight"]=collider.halfHeight;
            object["isTrigger"]=collider.isTrigger; object["layer"]=collider.layer; object["mask"]=collider.mask; object["model"]=collider.model;
            object["convex"]=collider.convex;
            components.push_back(object);
        }
        if (placement.rigidBody)
        {
            const auto& body=*placement.rigidBody;
            auto object=Component(body.id,"RigidBody",body.enabled);
            object["motion"]=body.motion; object["continuous"]=body.continuous;
            object["mass"]=body.mass; object["friction"]=body.friction; object["restitution"]=body.restitution;
            object["gravityScale"]=body.gravityScale; object["linearDamping"]=body.linearDamping; object["angularDamping"]=body.angularDamping;
            object["velocity"]=body.velocity; object["angularVelocity"]=body.angularVelocity;
            components.push_back(object);
        }
        for (const auto& script : placement.scripts)
        {
            auto object=Component(script.id,"Script",script.enabled);
            object["behaviour"]=script.behaviour; object["parameters"]=script.parameters;
            components.push_back(object);
        }
        if (placement.animator) components.push_back(WriteAnimator(*placement.animator));
        WriteEnvironmentComponents(components,placement);
        WriteUiComponents(components,placement);
        if (placement.animation) components.push_back(WriteAnimation(*placement.animation));
        return components;
    }
}
