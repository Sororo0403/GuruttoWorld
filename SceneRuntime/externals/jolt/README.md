# Jolt Physics

公式ソース: https://github.com/jrouwe/JoltPhysics/tree/e77f175595e64cb44218cc9d9d56fc365ad0e36a

Jolt 5.6.0を上記コミットへ固定します。MITライセンスです。
scripts/BuildJolt.ps1で取得・確認・ビルドし、SceneRuntime.libへ静的に結合します。
初回取得にネットワークが必要です。以後はgenerated/dependenciesのソースからオフラインでビルドできます。
DLL版MSVC CRTを使用し、Debug/Releaseを分けます。CPU要件はx64/SSE2です。
Samples・Viewer・GPU Compute・上流Profiler・Debug Rendererは無効です。
ライセンスをApp/EditorのLicensesへ同梱します。
