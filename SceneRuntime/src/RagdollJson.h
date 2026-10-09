#pragma once
#include <SceneRuntime/Ragdoll.h>
#include <Engine/Core/Json.h>
namespace SceneRuntime
{
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RagdollBone,bone,body,bindOffset)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RagdollComponent,id,enabled,active,weight,bodyMass,radius,swing,twist,bones)
}
