// X は混合の強さ、Y は黒・白・グレースケールの選択に使います。
// アルファと不透明描画の深度処理を、遷移の強さから独立させます。
#define PSMain TitleBasePSMain
#include "Mesh.hlsl"
#undef PSMain
float4 PSMain(VertexOutput input) : SV_TARGET
{
    float4 base = TitleBasePSMain(input);
    uint variant = (uint)floor(clamp(shaderParameters.y, 0, 2) + 0.5);
    float amount = floor(saturate(shaderParameters.x) * 1023 + 0.5) / 1023.0;
    float gray = dot(base.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 target = variant == 2 ? gray.xxx : (variant == 1 ? float3(1,1,1) : float3(0,0,0));
#ifdef TITLE_WHITE_TEXTURE_MASK
    // EXIT の緑の背景が黒くなっても、白い文字と矢印の読みやすさを保ちます。
    float3 texel = meshTexture.Sample(textureSampler, input.uv).rgb;
    if (variant == 1) target = step(0.92, min(texel.r, min(texel.g, texel.b))).xxx;
#endif
#ifdef TITLE_BUILDING_WINDOWS
    // Kenney の窓の配色は暗い灰色・青色（例: 110/115/138）で、
    // 壁のしっくいは 221/221/233 以上の明るさです。照明適用前のテクスチャを分類し、
    // 白い壁の影を黒い窓と誤判定しないようにします。
    float3 buildingTexel = meshTexture.Sample(textureSampler, input.uv).rgb;
    float wallMask = step(0.62, dot(buildingTexel, float3(0.2126, 0.7152, 0.0722)));
    if (variant == 1) target = wallMask.xxx;
#endif
    return float4(lerp(base.rgb, target, amount), 1);
}
