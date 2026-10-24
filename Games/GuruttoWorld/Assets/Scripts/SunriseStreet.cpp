#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <SceneRuntime/Project/GameSettings.h>
#include <Engine/Audio/AudioSystem.h>
#include <iterator>
#include "ParkScriptSchema.generated.h"
#include "SunriseScriptSchema.generated.h"
#include <algorithm>
#include <cmath>

bool Register_SunriseStreet(void*, SceneRuntime::RegisterModuleScript);
namespace
{
    using namespace SceneRuntime;
    using namespace SceneRuntime::GeneratedSchemas;
    struct SoundBank
    {
        Engine::AudioSystem audio;
        std::map<std::string, Engine::SoundHandle> handles;
        unsigned int users = 0;
        float gain = 1;
        bool ready = false, paused = false;
        static std::filesystem::path Root()
        {
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&Register_SunriseStreet), &module);
            std::array<wchar_t, 32768> path{};
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (length && length < path.size())
            {
                auto folder = std::filesystem::path(path.data()).parent_path();
                while (!folder.empty())
                {
                    if (std::filesystem::exists(folder / "Assets/Project.json"))
                        return folder;
                    const auto parent = folder.parent_path();
                    if (parent == folder)
                        break;
                    folder = parent;
                }
            }
            return std::filesystem::current_path() / "Games/GuruttoWorld";
        }
        Engine::SoundHandle Load(const std::string& clip, const std::filesystem::path& contentRoot)
        {
            if (!ready)
                return {};
            if (!handles.contains(clip))
                handles[clip] =
                    audio.Load((contentRoot.empty() ? Root() : contentRoot) / "Assets/Audio/Sunrise" / (clip + ".wav"));
            return handles.at(clip);
        }
        void Acquire(const std::string& clip, const std::filesystem::path& contentRoot)
        {
            if (users++ == 0)
            {
                ready = audio.Initialize();
                gain = GameSettings::Load(GameSettings::UserPath(contentRoot.empty() ? Root() : contentRoot)).Gain();
            }
            Load(clip, contentRoot);
        }
        void Release()
        {
            if (users && --users == 0)
            {
                audio.Shutdown();
                handles.clear();
                ready = false;
                paused = false;
            }
        }
        void Pause(bool value)
        {
            if (paused == value)
                return;
            paused = value;
            audio.PauseOutput(value);
            for (const auto& [clip, handle] : handles)
            {
                static_cast<void>(clip);
                audio.Pause(handle, value);
            }
        }
    };
    SoundBank bank;
    void Sound(ScriptContext& context, const std::string& clip)
    {
        ScriptEvent sound;
        sound.name = "sunrise:play";
        sound.sender = context.object.id;
        sound.target = "audio-" + clip;
        context.scene->Emit(std::move(sound));
    }
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
            {"discoveries", context.state.contains("discoveries") ? context.state.at("discoveries") : 0},
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
            Sound(context, "Collect");
        }
        for (const auto& id : Targets(context, "switches"))
        {
            const auto* item = context.scene->Find(id);
            if (!item || context.state["attack"] <= 0 ||
                Distance(context.object, *item) > context.Value("switchDistance", 2.7f) ||
                !VisibleTarget(context, *item))
                continue;
            context.scene->Destroy(id);
            context.state["switches"] += 1;
            Sound(context, "Mechanism");
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
            Sound(context, "Attack");
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
                Sound(context, "Hit");
            }
            else if (distance < context.Value("damageDistance", 1.15f) && context.state["invulnerable"] == 0 &&
                     VisibleTarget(context, *enemy))
            {
                context.state["hp"] -= context.Value("damage", 1);
                context.state["invulnerable"] = context.Value("invulnerableDuration", 1.5f);
                Sound(context, "Damage");
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

    void DiscoverRoof(ScriptContext& context, const std::array<float, 3>& p, bool shortBlock)
    {
        if (!shortBlock && p[0] < -4 && p[0] > -13 && p[1] > 3 && p[2] > 26 && p[2] < 40 &&
            context.state["roofFound"] == 0)
        {
            context.state["roofFound"] = 1;
            Sound(context, "Collect");
        }
    }

    void UpdateDiscoveries(ScriptContext& context)
    {
        // 発見は対象の空間へ実際に入ったときに記録します。視界に入るだけでは記録しません。
        const auto& p = context.object.position;
        const bool shortBlock = Text(context, "scene") == "Assets/Scenes/SunriseBlock.json";
        DiscoverRoof(context, p, shortBlock);
        if (!shortBlock && p[0] > 11.5f && p[0] < 17.5f && p[2] > 28.5f && p[2] < 37.5f &&
            context.state["roomFound"] == 0)
        {
            context.state["roomFound"] = 1;
            Sound(context, "Collect");
        }
        context.state["discoveries"] = context.state["roofFound"] + context.state["roomFound"];
    }

    std::string InteriorHint(const std::array<float, 3>& p, float progress, std::string message)
    {
        if (progress > 16 && progress < 42 && p[0] > 5)
            message = "商店の裏にふさがれた入口。J / X で箱をこわしてみよう";
        if (progress > 26 && progress < 40 && p[1] > 3)
            message = "カフェの屋上を発見！ 看板の裏も探して、中庭へ降りよう";
        if (p[0] > 11.5f && progress > 28 && progress < 38)
            message = "隠し部屋を発見！ 花を集めたら入口から商店通りへ戻ろう";
        return message;
    }

    void UpdatePlayerHint(ScriptContext& context, bool shortBlock)
    {
        const auto& p = context.object.position;
        const float progress = context.object.position[2];
        const auto hint = progress < context.Value("hintBoundary1", 14)   ? "hint1"
                          : progress < context.Value("hintBoundary2", 31) ? "hint2"
                          : progress < context.Value("hintBoundary3", 49) ? "hint3"
                                                                          : "hint4";
        std::string message = Text(context, hint);
        message = InteriorHint(p, progress, std::move(message));
        if (progress > 44 && progress < 62 && !context.scene->Find("switch-canal"))
            message = "橋がつながった！ 右の水路にも足場があるよ";
        if (progress > 76 && context.state["switches"] >= 2)
            message = "展望広場への道が開いた！ オレンジの目印へ";
        if (!shortBlock)
            SetText(context, Id(context, "hint"), message);
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
        Combat(context);
        Collect(context);
        UpdateDiscoveries(context);
        const bool shortBlock = Text(context, "scene") == "Assets/Scenes/SunriseBlock.json";
        const bool jumping = context.Pressed(Text(context, "jumpAction"));
        if (jumping)
            Sound(context, "Jump");
        context.scene->SetUiValue("sunriseHurt", context.state["invulnerable"] > 0 ? 1.0f : 0.0f);
        SetText(context, Id(context, "stats"), Format(context, Text(context, "statsTemplate")));
        UpdatePlayerHint(context, shortBlock);
        const auto* goal = context.Reference("goal");
        if (goal && context.state["switches"] >= context.Value("requiredSwitches", 2) &&
            Distance(context.object, *goal) < context.Value("goalDistance", 2))
        {
            context.state["clear"] = 1;
            context.scene->SetUiValue(Text(context, "clearState"), 1);
            context.scene->SetUiValue(Text(context, "attackState"), 0);
            SetText(context, Id(context, "result"), Format(context, Text(context, "resultTemplate")));
            Sound(context, "Clear");
        }
    }
    ScriptDefinition Player()
    {
        auto result = SunrisePlayerSchema();
        result.start = StartPlayer;
        result.fixedUpdate = FixedPlayer;
        result.update = UpdatePlayer;
        return result;
    }
    ScriptDefinition StreetPlayer()
    {
        auto result = Player();
        static_cast<ScriptSchema&>(result) = StreetPlayerSchema();
        result.update = [](ScriptContext& c)
        {
            if (!c.scene)
                return;
            if (c.Pressed(Text(c, "restartAction")))
            {
                c.scene->LoadScene(Text(c, "scene"));
                return;
            }
            if (c.Pressed(Text(c, "cancelAction")))
            {
                c.scene->LoadScene(Text(c, "returnScene"));
                return;
            }
            if (c.object.position[1] < c.Value("fallLimit", -3) || c.object.position[0] < c.Value("leftLimit", -36) ||
                c.object.position[0] > c.Value("rightLimit", 36))
                Respawn(c);
            const float x = c.Input(Text(c, "rightAction")) - c.Input(Text(c, "leftAction"));
            const float z = c.Input(Text(c, "forwardAction")) - c.Input(Text(c, "backAction"));
            if (std::hypot(x, z) > .1f)
                c.object.rotation[1] = std::atan2(x, z);
            if (c.Pressed(Text(c, "jumpAction")))
                Sound(c, "Jump");
            c.state["invulnerable"] = std::max(0.0f, c.state["invulnerable"] - static_cast<float>(c.seconds));
            Combat(c);
            Collect(c);
        };
        return result;
    }
    ScriptDefinition Camera()
    {
        auto result = SunriseCameraSchema();
        result.lateUpdate = [](ScriptContext& context)
        {
            const auto* target = context.Reference("target");
            if (!target || !context.scene || !target->parentId.empty() || !context.object.parentId.empty())
                return;
            const float z = target->position[2];
            const auto smooth = [](float a, float b, float v)
            {
                const float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
                return t * t * (3 - 2 * t);
            };
            const float water = smooth(40, 48, z) * (1 - smooth(60, 68, z));
            const float vista = smooth(94, 106, z);
            const float distance = context.Value("distance", 13) + water * 1.5f + vista * 2;
            const float elevation = std::max(0.0f, target->position[1] - 1.0f);
            const float height = context.Value("height", 10) + water * 2.5f + vista * 1.5f + elevation * .85f;
            const float blend = 1 - std::exp(-context.Value("response", 6) * static_cast<float>(context.seconds));
            const std::array<float, 3> wanted{
                target->position[0] * context.Value("horizontalFollow", .55f), height,
                std::clamp(z, context.Value("minZ", 2), std::max(context.Value("minZ", 2), context.Value("maxZ", 72))) -
                    distance};
            for (size_t axis = 0; axis < 3; ++axis)
                context.object.position[axis] += (wanted[axis] - context.object.position[axis]) * blend;
            const float dx = target->position[0] - context.object.position[0];
            const float dz = z + context.Value("lookAhead", 3) - context.object.position[2];
            context.object.rotation = {
                std::atan2(context.object.position[1] - (target->position[1] + .1f), std::hypot(dx, dz)),
                std::atan2(dx, dz), 0};
        };
        return result;
    }
    ScriptDefinition Patrol()
    {
        auto result = SunriseEnemySchema();
        result.start = [](ScriptContext& context)
        {
            context.state["originX"] = context.object.position[0];
            context.state["originZ"] = context.object.position[2];
        };
        result.update = [](ScriptContext& context)
        {
            const float dt = static_cast<float>(context.seconds);
            const auto* player = context.scene->Find("park-player");
            const float cycle = context.state["time"];
            context.state["time"] += dt;
            if (cycle < 1.2f)
            {
                context.object.scale = {1.25f, 1.15f, 1.25f};
                context.object.rotation[1] = std::sin(cycle * 3) * .3f;
                if (player && Distance(context.object, *player) < 7 && cycle > .7f)
                    context.state["time"] = 1.2f;
            }
            else if (cycle < 1.85f)
            {
                if (player)
                {
                    const float dx = player->position[0] - context.object.position[0],
                                dz = player->position[2] - context.object.position[2];
                    const float length = std::max(.01f, std::hypot(dx, dz));
                    context.state["dx"] = dx / length;
                    context.state["dz"] = dz / length;
                    context.object.rotation[1] = std::atan2(dx, dz);
                }
                context.object.scale = {1.45f, 0.85f, 1.45f};
            }
            else if (cycle < 2.45f)
            {
                context.object.scale = {1.1f, 1.15f, 1.4f};
                context.object.position[0] = std::clamp(context.object.position[0] + context.state["dx"] * dt * 3.5f,
                                                        context.state["originX"] - 2, context.state["originX"] + 2);
                context.object.position[2] = std::clamp(context.object.position[2] + context.state["dz"] * dt * 3.5f,
                                                        context.state["originZ"] - 2, context.state["originZ"] + 2);
            }
            else if (cycle > 3.6f)
                context.state["time"] = 0;
        };
        return result;
    }
    void ResolveBlockCamera(ScriptContext& c, const ScenePlacement* player, std::array<float, 3> wanted, float blend)
    {
        std::array<float, 3> delta{};
        for (size_t i = 0; i < 3; ++i)
            delta[i] = wanted[i] - player->position[i];
        const float length = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        auto direction = delta;
        std::transform(direction.begin(), direction.end(), direction.begin(),
                       [length](float value)
                       {
                           return value / length;
                       });
        const auto obstacle = c.scene->Raycast(player->position, direction, length, 0xffffffffu, false, player->id);
        if (obstacle)
        {
            const float fraction = std::clamp((obstacle->distance - .35f) / length, .02f, 1.0f);
            for (size_t i = 0; i < 3; ++i)
                wanted[i] = player->position[i] + delta[i] * fraction;
        }
        for (size_t i = 0; i < 3; ++i)
            c.object.position[i] += (wanted[i] - c.object.position[i]) * blend;
        // 目標位置に障害物がなくても、補間中に角の内側を通る可能性があるため、
        // 補間後の実際のカメラ位置にも衝突回避を適用します。
        for (size_t i = 0; i < 3; ++i)
            delta[i] = c.object.position[i] - player->position[i];
        const float actualLength = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        if (actualLength > .01f)
        {
            direction = delta;
            std::transform(direction.begin(), direction.end(), direction.begin(),
                           [actualLength](float value)
                           {
                               return value / actualLength;
                           });
            float safeLength = actualLength;
            for (float offset : {-.22f, 0.0f, .22f})
            {
                auto origin = player->position;
                origin[0] += offset;
                const auto hit = c.scene->Raycast(origin, direction, actualLength, 0xffffffffu, false, player->id);
                if (hit)
                    safeLength = std::min(safeLength, std::max(.12f, hit->distance - .35f));
            }
            if (safeLength < actualLength)
                for (size_t i = 0; i < 3; ++i)
                    c.object.position[i] = player->position[i] + direction[i] * safeLength;
        }
    }

    ScriptDefinition BlockCamera()
    {
        auto result = SunriseBlockCameraSchema();
        result.lateUpdate = [](ScriptContext& c)
        {
            const auto* player = c.Reference("target");
            if (!player || !c.scene)
                return;
            const float dt = static_cast<float>(c.seconds);
            const auto ground = c.scene->Raycast(player->position, {0, -1, 0}, 1.5f, 0xffffffffu, false, player->id);
            if (ground && ground->normal[1] > .7f)
                c.state["floor"] = ground->position[1];
            const float roof = std::clamp((player->position[2] - 79) / 9, 0.0f, 1.0f);
            const float distance = c.Value("distance", 7.8f) + roof * 1.2f;
            const float height = c.Value("height", 4.2f) + roof * .8f;
            // 広場では小さな寄り道でも前方を見る構図を保ちます。
            // 引き返す場合も連続的に補間し、
            // プレイヤーの向きや領域の切り替えで視点が反転しないようにします。
            const float t =
                std::clamp((player->position[2] - c.Value("plazaBlendStart", 18)) /
                               std::max(1.0f, c.Value("plazaBlendEnd", 34) - c.Value("plazaBlendStart", 18)),
                           0.0f, 1.0f);
            const float plaza = 1 - t * t * (3 - 2 * t);
            if (!c.state["framingReady"])
            {
                c.state["anchorX"] = player->position[0];
                c.state["framingReady"] = 1;
            }
            const float slack = c.Value("plazaFollowSlack", .85f) * plaza;
            c.state["anchorX"] =
                std::clamp(c.state["anchorX"], player->position[0] - slack, player->position[0] + slack);
            const float anchor = c.state["anchorX"];
            const float aimX = anchor + (c.Value("plazaExitX", 8) - anchor) * c.Value("plazaFocusWeight", .22f) * plaza;
            std::array<float, 3> wanted{anchor - .5f * plaza, c.state["floor"] + .7f + height,
                                        player->position[2] - distance};
            // カメラはカフェのひさしの外側を通します。プレイヤーを追ってひさしの下へ
            // 入ると、布がプレイヤーを隠してしまうためです。
            wanted[0] = std::max(wanted[0], c.Value("plazaCameraLeft", -3) * plaza + player->position[0] * (1 - plaza));
            const float blend = 1 - std::exp(-c.Value("response", 7) * dt);
            ResolveBlockCamera(c, player, wanted, blend);
            // 構図の補間は、衝突を避ける位置の調整から独立させます。
            // 壁を避けて視点を即座に近づけても、注視方向は急に回転させません。
            if (!c.state["aimReady"])
            {
                c.state["aimX"] = aimX;
                c.state["aimReady"] = 1;
            }
            c.state["aimX"] += (aimX - c.state["aimX"]) * blend;
            const float dx = c.state["aimX"] - c.object.position[0];
            const float dz = player->position[2] + c.Value("lookAhead", 4.5f) + roof * 2 - c.object.position[2];
            c.object.rotation = {std::atan2(c.object.position[1] - (c.state["floor"] + 1.1f), std::hypot(dx, dz)),
                                 std::atan2(dx, dz), 0};
        };
        return result;
    }
    ScriptDefinition BlockPlayer()
    {
        auto result = Player();
        static_cast<ScriptSchema&>(result) = SunriseBlockPlayerSchema();
        result.update = [](ScriptContext& c)
        {
            UpdatePlayer(c);
            if (!c.scene || c.state["clear"] > 0 || c.Pressed(Text(c, "restartAction")) ||
                c.Pressed(Text(c, "cancelAction")))
                return;
            const auto& p = c.object.position;
            if (p[2] < -9 || p[2] > 101)
                Respawn(c);
        };
        return result;
    }
    ScriptDefinition Shutter()
    {
        auto result = SunriseShutterSchema();
        result.start = [](ScriptContext& c)
        {
            c.state["x"] = c.object.position[0];
        };
        result.update = [](ScriptContext& c)
        {
            if (c.Reference("switch"))
                return;
            c.state["t"] = std::min(1.0f, c.state["t"] + static_cast<float>(c.seconds));
            const float t = c.state["t"];
            c.object.position[0] = c.state["x"] - 5 * t * t * (3 - 2 * t);
        };
        return result;
    }
    ScriptDefinition Mechanism()
    {
        auto result = SunriseMechanismSchema();
        result.update = [](ScriptContext& c)
        {
            if (c.Reference("switch"))
                return;
            c.state["time"] += static_cast<float>(c.seconds);
            c.object.rotation[2] += static_cast<float>(c.seconds) * 3;
            c.scene->SetUiValue(c.object.id, 1);
        };
        return result;
    }
    ScriptDefinition Bridge()
    {
        auto result = SunriseBridgeSchema();
        result.start = [](ScriptContext& c)
        {
            c.state["x"] = c.object.position[0];
        };
        result.update = [](ScriptContext& c)
        {
            if (c.Reference("switch"))
                return;
            c.state["t"] = std::min(1.0f, c.state["t"] + static_cast<float>(c.seconds) * 1.4f);
            const float t = c.state["t"];
            c.object.position[0] = c.state["x"] * (1 - t * t * (3 - 2 * t));
        };
        return result;
    }
    ScriptDefinition Hero()
    {
        auto result = SunriseHeroSchema();
        result.update = [](ScriptContext& c)
        {
            c.state["t"] += static_cast<float>(c.seconds);
            const bool moving =
                c.Input("MoveForward") + c.Input("MoveBack") + c.Input("MoveLeft") + c.Input("MoveRight") > .1f;
            c.object.position[1] =
                moving ? std::abs(std::sin(c.state["t"] * 13)) * .07f : std::sin(c.state["t"] * 2) * .025f;
            const float squash = c.Pressed("Attack") ? .82f : 1;
            c.object.scale = {1 / squash, squash, 1 / squash};
        };
        return result;
    }
    ScriptDefinition Lifetime()
    {
        auto result = SunriseSoundSchema();
        result.start = [](const ScriptContext& c)
        {
            bank.Acquire(Text(c, "clip"), c.contentRoot);
        };
        result.stop = [](const ScriptStopContext&)
        {
            bank.Release();
        };
        result.onEvent = [](ScriptContext& c)
        {
            if (c.event && c.event->name == "sunrise:play" && bank.ready)
            {
                const auto handle = bank.Load(Text(c, "clip"), c.contentRoot);
                bank.audio.SetVolume(handle, c.Value("volume", .35f) * bank.gain);
                if (bank.audio.Play(handle, false))
                {
                    if (bank.paused)
                        bank.audio.Pause(handle, true);
                    c.state["plays"] += 1;
                    c.scene->SetUiValue(c.object.id, c.state["plays"]);
                }
            }
        };
        result.update = [](ScriptContext&)
        {
            if (bank.ready)
                bank.audio.Update();
        };
        result.pause = [](ScriptContext&, bool paused)
        {
            bank.Pause(paused);
        };
        return result;
    }
}

bool Register_SunriseStreet(void* owner, SceneRuntime::RegisterModuleScript add)
{
    const auto player = Player(), camera = Camera(), patrol = Patrol();
    const auto mechanism = Mechanism(), bridge = Bridge(), hero = Hero(), lifetime = Lifetime();
    const auto blockPlayer = BlockPlayer(), blockCamera = BlockCamera(), shutter = Shutter();
    const auto streetPlayer = StreetPlayer();
    return add(owner, "SunrisePlayer", &player) && add(owner, "SunriseCamera", &camera) &&
           add(owner, "SunriseEnemy", &patrol) && add(owner, "SunriseMechanism", &mechanism) &&
           add(owner, "SunriseBridge", &bridge) && add(owner, "SunriseHero", &hero) &&
           add(owner, "SunriseSound", &lifetime) && add(owner, "SunriseBlockPlayer", &blockPlayer) &&
           add(owner, "SunriseBlockCamera", &blockCamera) && add(owner, "SunriseShutter", &shutter) &&
           add(owner, "StreetPlayer", &streetPlayer);
}
