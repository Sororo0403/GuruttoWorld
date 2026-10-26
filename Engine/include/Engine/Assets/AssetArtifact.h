#pragma once
#include <filesystem>
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
        std::string Canonical() const;
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
    };
    class ArtifactSources final
    {
    public:
        /// <summary>存在しない任意依存も含め、参照したファイル内容の版を固定します。</summary>
        static ArtifactSources Capture(const std::vector<std::pair<std::string, std::filesystem::path>>& files);
        /// <summary>固定した依存を成果物の入力へ追加します。</summary>
        void Describe(ArtifactDescriptor& descriptor) const;
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
        /// <summary>再生成できる成果物の保存先を固定します。</summary>
        explicit ArtifactStore(std::filesystem::path root);
        /// <summary>キー・サイズ・内容のハッシュが一致する成果物だけを読みます。破損は未登録として扱います。</summary>
        std::shared_ptr<const ArtifactData> Read(const std::string& key) const noexcept;
        /// <summary>検証した成果物を一時ファイルから原子的に公開します。同時要求の一時名は共有しません。</summary>
        bool Write(const ArtifactData& data) const noexcept;

    private:
        std::filesystem::path root_;
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
    class ArtifactRequests final
    {
    public:
        using Build = std::function<std::string()>;
        /// <summary>プロジェクトやセッション単位の公開世代と成果物の保持先を作ります。</summary>
        explicit ArtifactRequests(std::shared_ptr<const ArtifactStore> store = {});
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
        /// <summary>要求の所有権を複製できません。</summary>
        ArtifactRequests(const ArtifactRequests&) = delete;
        /// <summary>要求の所有権を代入できません。</summary>
        ArtifactRequests& operator=(const ArtifactRequests&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
