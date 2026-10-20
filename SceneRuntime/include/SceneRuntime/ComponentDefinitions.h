#pragma once

// 保存するコンポーネントの順序・多重度・参照の有無・互換保存順を一箇所で宣言します。
#define ENGINE_SCENE_COMPONENTS(OPTIONAL, REPEATED)                                                                    \
    OPTIONAL(MeshRendererComponent, meshRenderer, false, true, 1)                                                      \
    OPTIONAL(MaterialComponent, material, false, true, 2)                                                              \
    OPTIONAL(RotatorComponent, rotator, false, false, 3)                                                               \
    OPTIONAL(PlayerControllerComponent, playerController, false, false, 4)                                             \
    OPTIONAL(BoxColliderComponent, boxCollider, false, true, 5)                                                        \
    OPTIONAL(RigidBodyComponent, rigidBody, false, false, 6)                                                           \
    OPTIONAL(JointComponent, joint, true, false, 0)                                                                    \
    REPEATED(ScriptComponent, scripts, true, true, 7)                                                                  \
    OPTIONAL(CameraComponent, camera, false, false, 10)                                                                \
    OPTIONAL(DirectionalLightComponent, directionalLight, false, false, 11)                                            \
    OPTIONAL(PointLightComponent, pointLight, false, false, 12)                                                        \
    OPTIONAL(SpotLightComponent, spotLight, false, false, 13)                                                          \
    OPTIONAL(SkyComponent, sky, false, false, 14)                                                                      \
    OPTIONAL(ParticleEmitterComponent, particleEmitter, false, false, 15)                                              \
    OPTIONAL(CameraSwayComponent, cameraSway, false, false, 16)                                                        \
    OPTIONAL(CanvasComponent, canvas, true, true, 23)                                                                  \
    OPTIONAL(RectTransformComponent, rectTransform, false, false, 24)                                                  \
    OPTIONAL(ImageComponent, image, false, true, 25)                                                                   \
    OPTIONAL(TextComponent, text, false, false, 26)                                                                    \
    OPTIONAL(ButtonComponent, button, true, true, 27)                                                                  \
    OPTIONAL(InputFieldComponent, inputField, false, false, 17)                                                        \
    OPTIONAL(SliderComponent, slider, false, false, 18)                                                                \
    OPTIONAL(ToggleComponent, toggle, false, false, 19)                                                                \
    OPTIONAL(ScrollViewComponent, scrollView, false, false, 20)                                                        \
    OPTIONAL(MaskComponent, mask, false, false, 21)                                                                    \
    OPTIONAL(LayoutGroupComponent, layoutGroup, false, false, 22)                                                      \
    OPTIONAL(TerrainComponent, terrain, false, true, 31)                                                               \
    OPTIONAL(TilemapComponent, tilemap, false, true, 32)                                                               \
    OPTIONAL(NavMeshComponent, navMesh, false, false, 33)                                                              \
    OPTIONAL(NavAgentComponent, navAgent, true, false, 34)                                                             \
    OPTIONAL(AudioSourceComponent, audioSource, false, true, 28)                                                       \
    OPTIONAL(AudioListenerComponent, audioListener, false, false, 29)                                                  \
    OPTIONAL(AudioMixerComponent, audioMixer, false, false, 30)                                                        \
    OPTIONAL(AnimationComponent, animation, false, false, 35)                                                          \
    OPTIONAL(AnimatorComponent, animator, false, false, 8)                                                             \
    OPTIONAL(RagdollComponent, ragdoll, true, false, 9)
