#pragma once
#include <SceneRuntime/Scene/SceneChanges.h>
#include <SceneRuntime/Scene/SceneLayout.h>
#include <optional>
#include <string_view>
#include <memory>

namespace SceneRuntime
{
    /// <summary>所有者の変更まで有効な借用ビューです。取得した値には可変参照を含めません。</summary>
    class WorldReadView final
    {
    public:
        class Source
        {
        public:
            /// <summary>読み取り元の借用情報を解放します。World自体は所有しません。</summary>
            virtual ~Source() = default;
            /// <summary>呼出元の変更まで有効な、読み取り専用の個体を取得します。</summary>
            virtual const ScenePlacement* Find(std::string_view id) const = 0;
            /// <summary>個体の独立した値を取得します。</summary>
            virtual std::optional<ScenePlacement> Object(std::string_view id) const = 0;
            /// <summary>数値姿勢だけを取得します。</summary>
            virtual std::optional<LocalTransform> Transform(std::string_view id) const = 0;
            /// <summary>現在の個体の順序に従う保存IDを取得します。</summary>
            virtual std::vector<std::string> ObjectIds() const = 0;
            /// <summary>現在の設定を値として取得します。</summary>
            virtual SceneSettings Settings() const = 0;
        };
        /// <summary>生存中の配置を借用します。所有者の変更後はビューを取得し直してください。</summary>
        explicit WorldReadView(const SceneLayout& layout) noexcept;
        /// <summary>保存型を正本にしない読み取り元から、同じ値の契約を提供します。</summary>
        explicit WorldReadView(std::shared_ptr<const Source> source);
        /// <summary>一時的な配置からの借用を禁止します。</summary>
        WorldReadView(SceneLayout&&) = delete;
        /// <summary>読み取り専用の一時配置からの借用も禁止します。</summary>
        WorldReadView(const SceneLayout&&) = delete;

        /// <summary>指定IDのローカル姿勢を値として取得し、存在しない場合は未設定を返します。</summary>
        std::optional<LocalTransform> Transform(std::string_view id) const;
        /// <summary>個体を独立した値として取得します。存在しないIDは未設定です。</summary>
        std::optional<ScenePlacement> Object(std::string_view id) const;
        /// <summary>現在の順序に従う個体IDの一覧を取得します。</summary>
        std::vector<std::string> ObjectIds() const;
        /// <summary>現在の保存設定を値として取得します。</summary>
        SceneSettings Settings() const;
        /// <summary>型付きコンポーネントを値として取得し、可変参照を公開しません。</summary>
        template <class T>
        std::optional<T> Component(std::string_view id, std::optional<T> ScenePlacement::* member) const
        {
            const auto* object = Find(id);
            return object ? object->*member : std::nullopt;
        }

    private:
        /// <summary>ビューの内部だけで借用個体を参照します。</summary>
        const ScenePlacement* Find(std::string_view id) const;
        const SceneLayout* layout_ = nullptr;
        std::shared_ptr<const Source> source_;
    };
}
