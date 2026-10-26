#pragma once
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Engine
{
    struct ArtifactDependency
    {
        std::string id, revision;
        /// <summary>依存の識別子と内容の版を比較します。</summary>
        bool operator==(const ArtifactDependency&) const = default;
    };
    struct ArtifactDescriptor
    {
        std::string kind, assetId, sourceRevision, importerVersion, settings, platform, compiler;
        std::vector<ArtifactDependency> dependencies;
        /// <summary>区切り文字の衝突を避け、依存の列挙順に左右されない成果物の入力を構築します。</summary>
        std::string Canonical() const
        {
            if (kind.empty() || assetId.empty() || sourceRevision.empty() || importerVersion.empty() ||
                platform.empty())
                throw std::invalid_argument("Incomplete artifact descriptor");
            std::string result = "WP1ART02";
            const auto append = [&](std::string_view value)
            {
                result += std::to_string(value.size());
                result += ':';
                result += value;
            };
            for (const auto* value :
                 {&kind, &assetId, &sourceRevision, &importerVersion, &settings, &platform, &compiler})
                append(*value);
            auto ordered = dependencies;
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto& a, const auto& b)
                      {
                          return a.id < b.id;
                      });
            std::string previous;
            bool first = true;
            for (const auto& dependency : ordered)
            {
                if (dependency.id.empty() || dependency.revision.empty() || (!first && dependency.id == previous))
                    throw std::invalid_argument("Invalid or duplicate artifact dependency");
                append(dependency.id);
                append(dependency.revision);
                previous = dependency.id;
                first = false;
            }
            return result;
        }
        /// <summary>入力全体のSHA-256を成果物のキーとして取得します。</summary>
        std::string Key() const;
    };
    class ArtifactHash final
    {
    public:
        /// <summary>バイナリを含む内容のSHA-256を取得します。</summary>
        static std::string Bytes(std::string_view bytes);
        /// <summary>ファイル内容のSHA-256を取得します。読み取れない場合は例外を返します。</summary>
        static std::string File(const std::filesystem::path& path);
    };
    struct ArtifactSource
    {
        std::string id;
        std::filesystem::path path;
        std::optional<std::string> revision;
        bool keyInput = true;
    };
    class ArtifactSources final
    {
    public:
        /// <summary>存在しない任意依存も含め、参照したファイル内容の版を固定します。</summary>
        static ArtifactSources Capture(const std::vector<std::pair<std::string, std::filesystem::path>>& files);
        /// <summary>意味を変えない補助情報をキーから除き、取り込み中の版の検証だけに使用します。</summary>
        void ProtectOnly(const std::string& id);
        /// <summary>固定した依存を成果物の入力へ追加します。</summary>
        void Describe(ArtifactDescriptor& descriptor) const;
        /// <summary>cookが同じ入力の証明を保存するため、不変の取得済み入力を読みます。</summary>
        const std::vector<ArtifactSource>& Inputs() const noexcept
        {
            return files_;
        }
        /// <summary>日時ではなく内容を再照合し、取り込み中の変更や後から追加された依存を検出します。</summary>
        bool Unchanged() const noexcept;

    private:
        std::vector<ArtifactSource> files_;
    };
    struct ArtifactData
    {
        std::string key, bytes;
    };
    class ArtifactStore final
    {
    public:
        static constexpr size_t MaximumBytes = 256 * 1024 * 1024;
        /// <summary>再生成できる成果物の保存先を固定します。</summary>
        explicit ArtifactStore(std::filesystem::path root, std::filesystem::path fallback = {});
        /// <summary>キー・サイズ・内容のハッシュが一致する成果物だけを読みます。破損は未登録として扱います。</summary>
        std::shared_ptr<const ArtifactData> Read(const std::string& key) const noexcept;
        /// <summary>検証した成果物を一時ファイルから原子的に公開します。同時要求の一時名は共有しません。</summary>
        bool Write(const ArtifactData& data) const noexcept;

    private:
        std::filesystem::path root_, fallback_;
    };
    class ArtifactTicket final
    {
    public:
        /// <summary>未設定の要求を作ります。</summary>
        ArtifactTicket() = default;
        /// <summary>完了したCPU成果物を取得します。実行中・失敗・取消では未設定です。</summary>
        std::shared_ptr<const ArtifactData> Poll() const;
        /// <summary>要求が完了したかを返します。成功判定にはPollを使用します。</summary>
        bool Ready() const;
        /// <summary>完了時の診断を取得します。</summary>
        std::string Error() const;

    private:
        friend class ArtifactRequests;
        struct State;
        std::shared_ptr<State> state_;
    };
    class AssetImportScope final
    {
    public:
        /// <summary>アプリの生存期間中の非同期取り込みを、共有キャッシュの破棄前に終了させます。</summary>
        AssetImportScope() = default;
        /// <summary>登録済みのCPU取り込みジョブがすべて終了するまで待ちます。</summary>
        ~AssetImportScope();
        /// <summary>終了待機の所有権を複製できません。</summary>
        AssetImportScope(const AssetImportScope&) = delete;
        /// <summary>終了待機の所有権を代入できません。</summary>
        AssetImportScope& operator=(const AssetImportScope&) = delete;
    };
    class ArtifactRequests final
    {
    public:
        using Build = std::function<std::string()>;
        /// <summary>プロジェクトやセッション単位の公開世代と成果物の保持先を作ります。</summary>
        explicit ArtifactRequests(std::shared_ptr<const ArtifactStore> store = {}, bool retainPublished = true);
        /// <summary>未完了のCPU処理の終了を待って所有資源を解放します。GPU処理は実行しません。</summary>
        ~ArtifactRequests();
        /// <summary>同じ入力の同時要求をまとめ、異なる入力では先行要求を失効させます。</summary>
        ArtifactTicket Request(ArtifactDescriptor descriptor, ArtifactSources sources, Build build);
        /// <summary>現在の所有者・入力世代と一致し、取り込み元が変わっていない要求だけを受け付けます。</summary>
        bool Publish(const ArtifactTicket& ticket);
        /// <summary>失敗した新しい要求でも、以前の確定済み成果物は保持します。</summary>
        std::shared_ptr<const ArtifactData> Published(const std::string& assetId) const;
        /// <summary>指定素材の未完了要求を失効させます。確定済みの世代は保持します。</summary>
        void Cancel(const std::string& assetId);
        /// <summary>呼出し時点のCPUジョブを待ちます。待機中も公開用のロックを保持しません。</summary>
        void WaitForIdle();
        /// <summary>要求の所有権を複製できません。</summary>
        ArtifactRequests(const ArtifactRequests&) = delete;
        /// <summary>要求の所有権を代入できません。</summary>
        ArtifactRequests& operator=(const ArtifactRequests&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
