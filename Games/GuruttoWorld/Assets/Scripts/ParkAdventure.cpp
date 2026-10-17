#include <SceneRuntime/ScriptModuleApi.h>
#include <iterator>
#include "ParkScriptSchema.generated.h"
#include <algorithm>
#include <cmath>

namespace
{
    using namespace SceneRuntime;
    std::string Text(const ScriptContext& context, const std::string& key)
    {
        const auto* value = context.Data(key);
        return value ? std::get<std::string>(value->value) : std::string{};
    }
    std::string Id(const ScriptContext& context, const std::string& key)
    {
        const auto* value = context.Data(key);
        return value ? std::get<ScriptObjectReference>(value->value).id : std::string{};
    }
    std::vector<std::string> Targets(const ScriptContext& context, const std::string& key)
    {
        std::vector<std::string> ids;
        const auto* value = context.Data(key);
        if (value)
            for (const auto& item : std::get<ScriptValue::Array>(value->value))
                if (const auto* reference = std::get_if<ScriptObjectReference>(&item.value);
                    reference && !reference->id.empty())
                    ids.push_back(reference->id);
        if (ids.empty() && std::get<bool>(context.Data("useNameLookup")->value) && !Text(context, key + "Name").empty())
            return context.scene->FindByName(Text(context, key + "Name"));
        return ids;
    }
    std::string Format(const ScriptContext& context, std::string text)
    {
        const std::pair<const char*, float> values[] = {
            {"hp", context.state.at("hp")},
            {"coins", context.state.contains("coins") ? context.state.at("coins") : 0},
            {"switches", context.state.contains("switches") ? context.state.at("switches") : 0},
            {"elapsed", context.state.contains("elapsed") ? context.state.at("elapsed") : 0},
            {"coinTotal", context.Value("coinTotal", 12)},
            {"requiredSwitches", context.Value("requiredSwitches", 2)}};
        for (const auto& [key, value] : values)
        {
            const auto token = "{" + std::string(key) + "}", replacement = std::to_string(static_cast<int>(value));
            size_t at = 0;
            while ((at = text.find(token, at)) != std::string::npos)
            {
                text.replace(at, token.size(), replacement);
                at += replacement.size();
            }
        }
        return text;
    }
    float Distance(const ScenePlacement& first, const ScenePlacement& second)
    {
        const auto& a = first.position;
        const auto& b = second.position;
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    }
    void SetText(ScriptContext& context, const std::string& id, const std::string& text)
    {
        const auto* source = context.scene->Find(id);
        if (!source || !source->text || source->text->text == text)
            return;
        auto changed = *source;
        changed.text->text = text;
        context.scene->SetComponents(id, changed);
    }
    bool VisibleTarget(ScriptContext& context, const ScenePlacement& target)
    {
        std::array<float, 3> direction{};
        for (size_t axis = 0; axis < 3; ++axis)
            direction[axis] = target.position[axis] - context.object.position[axis];
        const float distance = Distance(context.object, target);
        if (distance < 0.001f)
            return true;
        std::transform(direction.begin(), direction.end(), direction.begin(),
                       [distance](float value)
                       {
                           return value / distance;
                       });
        const auto hit =
            context.scene->Raycast(context.object.position, direction, distance, 0xffffffffu, false, context.object.id);
        return !hit || hit->object == target.id;
    }
    void Respawn(ScriptContext& context)
    {
        context.object.position = {context.state["spawnX"], context.Value("respawnHeight", 1.05f),
                                   context.state["spawnZ"]};
        context.state["invulnerable"] = context.Value("invulnerableDuration", 1.5f);
        context.state["floatTime"] = 0;
        context.state["resetVelocity"] = 1;
        context.state["hp"] = context.Value("maxHp", 3);
    }
    void Collect(ScriptContext& context)
    {
        for (const auto& id : Targets(context, "coins"))
        {
            const auto* coin = context.scene->Find(id);
            if (!coin || Distance(context.object, *coin) > context.Value("coinDistance", 1.25f) ||
                !VisibleTarget(context, *coin))
                continue;
            context.scene->Destroy(id);
            context.state["coins"] += 1;
        }
        for (const auto& id : Targets(context, "switches"))
        {
            const auto* item = context.scene->Find(id);
            if (!item || Distance(context.object, *item) > context.Value("switchDistance", 1.55f) ||
                !VisibleTarget(context, *item))
                continue;
            context.scene->Destroy(id);
            context.state["switches"] += 1;
        }
        if (context.state["switches"] >= context.Value("requiredSwitches", 2) && context.Reference("gate"))
            context.scene->Destroy(Id(context, "gate"));
        for (const auto& id : Targets(context, "checkpoints"))
        {
            const auto* item = context.scene->Find(id);
            if (item && Distance(context.object, *item) < context.Value("checkpointDistance", 1.6f))
            {
                context.state["spawnX"] = item->position[0];
                context.state["spawnZ"] = item->position[2];
            }
        }
    }
    void Combat(ScriptContext& context)
    {
        context.state["attack"] = std::max(0.0f, context.state["attack"] - static_cast<float>(context.seconds));
        context.state["cooldown"] = std::max(0.0f, context.state["cooldown"] - static_cast<float>(context.seconds));
        if (context.Pressed(Text(context, "attackAction")) && context.state["cooldown"] == 0)
        {
            context.state["attack"] = context.Value("attackDuration", .22f);
            context.state["cooldown"] = context.Value("attackCooldown", .4f);
        }
        for (const auto& id : Targets(context, "enemies"))
        {
            const auto* enemy = context.scene->Find(id);
            if (!enemy)
                continue;
            const float distance = Distance(context.object, *enemy);
            if (context.state["attack"] > 0 && distance < context.Value("attackDistance", 3.1f) &&
                VisibleTarget(context, *enemy))
            {
                context.scene->Destroy(id);
                context.state["defeated"] += 1;
            }
            else if (distance < context.Value("damageDistance", 1.15f) && context.state["invulnerable"] == 0 &&
                     VisibleTarget(context, *enemy))
            {
                context.state["hp"] -= context.Value("damage", 1);
                context.state["invulnerable"] = context.Value("invulnerableDuration", 1.5f);
            }
        }
        if (context.state["hp"] <= 0)
            Respawn(context);
        context.scene->SetUiValue(Text(context, "attackState"), context.state["attack"] > 0 ? 1.0f : 0.0f);
    }
    void StartPlayer(ScriptContext& context)
    {
        context.state["hp"] = context.Value("maxHp", 3);
        context.state["spawnX"] = context.object.position[0];
        context.state["spawnZ"] = context.object.position[2];
    }

    void FixedPlayer(ScriptContext& context)
    {
        if (!context.scene || !context.object.playerController)
            return;
        auto& controller = *context.object.playerController;
        const auto ground =
            context.scene->Raycast(context.object.position, {0, -1, 0}, context.Value("groundProbe", .8f), 0xffffffffu,
                                   false, context.object.id);
        const float dt = static_cast<float>(context.seconds);
        if (ground && ground->normal[1] > context.Value("groundNormal", .7f))
        {
            context.state["floatTime"] = 0;
            context.state["airTime"] = 0;
        }
        else
            context.state["airTime"] += dt;
        const bool floating = !ground && context.state["airTime"] > context.Value("floatDelay", .25f) &&
                              context.Input(Text(context, "jumpAction")) > 0.5f &&
                              !context.Pressed(Text(context, "jumpAction")) &&
                              context.state["floatTime"] < context.Value("floatDuration", 0.85f);
        controller.useGravity = !floating && context.state["resetVelocity"] == 0;
        if (floating)
            context.state["floatTime"] += dt;
        context.state["resetVelocity"] = 0;
        if (context.state["clear"] > 0)
            controller.moveSpeed = 0;
    }

    void UpdatePlayer(ScriptContext& context)
    {
        if (!context.scene)
            return;
        if (context.Pressed(Text(context, "restartAction")))
        {
            context.scene->LoadScene(std::get<std::string>(context.Data("scene")->value));
            return;
        }
        if (context.Pressed(Text(context, "cancelAction")))
        {
            context.scene->LoadScene(Text(context, "returnScene"));
            return;
        }
        if (context.state["clear"] > 0)
            return;
        const float dt = static_cast<float>(context.seconds);
        context.state["elapsed"] += dt;
        context.state["invulnerable"] = std::max(0.0f, context.state["invulnerable"] - dt);
        if (context.object.position[1] < context.Value("fallLimit", -3) ||
            context.object.position[0] < context.Value("leftLimit", -20) ||
            context.object.position[0] > context.Value("rightLimit", 20))
            Respawn(context);
        const float x = context.Input(Text(context, "rightAction")) - context.Input(Text(context, "leftAction"));
        const float z = context.Input(Text(context, "forwardAction")) - context.Input(Text(context, "backAction"));
        if (std::hypot(x, z) > 0.1f)
            context.object.rotation[1] = std::atan2(x, z);
        Collect(context);
        Combat(context);
        SetText(context, Id(context, "stats"), Format(context, Text(context, "statsTemplate")));
        const float progress = context.object.position[2];
        const auto hint = progress < context.Value("hintBoundary1", 14)   ? "hint1"
                          : progress < context.Value("hintBoundary2", 31) ? "hint2"
                          : progress < context.Value("hintBoundary3", 49) ? "hint3"
                                                                          : "hint4";
        SetText(context, Id(context, "hint"), Text(context, hint));
        const auto* goal = context.Reference("goal");
        if (goal && context.state["switches"] >= context.Value("requiredSwitches", 2) &&
            Distance(context.object, *goal) < context.Value("goalDistance", 2))
        {
            context.state["clear"] = 1;
            context.scene->SetUiValue(Text(context, "clearState"), 1);
            context.scene->SetUiValue(Text(context, "attackState"), 0);
            SetText(context, Id(context, "result"), Format(context, Text(context, "resultTemplate")));
        }
    }
    ScriptDefinition Player()
    {
        auto result = ParkPlayerSchema();
        result.start = StartPlayer;
        result.fixedUpdate = FixedPlayer;
        result.update = UpdatePlayer;
        return result;
    }
    ScriptDefinition Camera()
    {
        auto result = ParkCameraSchema();
        result.lateUpdate = [](ScriptContext& context)
        {
            const auto* target = context.Reference("target");
            if (!target || !context.scene || !target->parentId.empty() || !context.object.parentId.empty())
                return;
            const float z = target->position[2];
            const float distance = context.Value("distance", 13);
            const float height = context.Value("height", 10);
            const float blend = 1 - std::exp(-context.Value("response", 6) * static_cast<float>(context.seconds));
            const std::array<float, 3> wanted{
                target->position[0] * context.Value("horizontalFollow", .55f), height,
                std::clamp(z, context.Value("minZ", 2), std::max(context.Value("minZ", 2), context.Value("maxZ", 72))) -
                    distance};
            for (size_t axis = 0; axis < 3; ++axis)
                context.object.position[axis] += (wanted[axis] - context.object.position[axis]) * blend;
            const float dx = target->position[0] - context.object.position[0];
            const float dz = z + context.Value("lookAhead", 3) - context.object.position[2];
            context.object.rotation = {std::atan2(height - context.Value("lookHeight", .8f), std::hypot(dx, dz)),
                                       std::atan2(dx, dz), 0};
        };
        return result;
    }
    ScriptDefinition Patrol()
    {
        auto result = ParkPatrolSchema();
        result.start = [](ScriptContext& context)
        {
            context.state["originX"] = context.object.position[0];
        };
        result.update = [](ScriptContext& context)
        {
            context.state["time"] += static_cast<float>(context.seconds);
            context.object.position[0] = context.state["originX"] + std::sin(context.state["time"] * 6.2831853f *
                                                                             context.Value("frequency", 0.25f)) *
                                                                        context.Value("amplitude", 1);
        };
        return result;
    }
}

bool Register_ParkAdventure(void* owner, SceneRuntime::RegisterModuleScript add)
{
    const auto player = Player(), camera = Camera(), patrol = Patrol();
    return add(owner, "ParkPlayer", &player) && add(owner, "ParkCamera", &camera) && add(owner, "ParkPatrol", &patrol);
}
