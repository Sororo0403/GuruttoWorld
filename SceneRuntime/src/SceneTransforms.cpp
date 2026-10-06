#include <SceneRuntime/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace
{
    using Matrix=DirectX::XMFLOAT4X4;
    std::vector<std::optional<size_t>> ParentIndices(const SceneRuntime::SceneLayout& layout)
    {
        std::unordered_map<std::string,size_t> indices;
        for (size_t index=0;index<layout.objects.size();++index)
        {
            const auto& id=layout.objects[index].id;
            if (id.empty() || !indices.emplace(id,index).second) throw std::runtime_error("Empty or duplicate transform ID: "+id);
        }
        std::vector<std::optional<size_t>> parents(layout.objects.size());
        for (size_t index=0;index<layout.objects.size();++index)
        {
            const auto& parent=layout.objects[index].parentId;
            if (parent.empty()) continue;
            const auto found=indices.find(parent);
            if (found==indices.end()) throw std::runtime_error("Missing transform parent: "+parent);
            parents[index]=found->second;
        }
        return parents;
    }
    bool Finite(const Matrix& matrix)
    {
        return std::all_of(std::begin(matrix.m),std::end(matrix.m),[](const auto& row)
        {
            return std::all_of(std::begin(row),std::end(row),[](float value) { return std::isfinite(value); });
        });
    }
    void ResolveChain(size_t start, const std::vector<std::optional<size_t>>& parents,
        SceneRuntime::TransformSpace space, std::vector<unsigned char>& visited, std::vector<Matrix>& matrices)
    {
        std::vector<size_t> chain;
        std::optional<size_t> current=start;
        while (current && visited[*current]==0)
        {
            visited[*current]=1;
            chain.push_back(*current);
            current=parents[*current];
        }
        if (current && visited[*current]==1) throw std::runtime_error("Transform parent cycle detected");
        for (auto item=chain.rbegin();item!=chain.rend();++item)
        {
            const auto index=*item;
            if (space==SceneRuntime::TransformSpace::Local && parents[index])
            {
                DirectX::XMStoreFloat4x4(&matrices[index],DirectX::XMLoadFloat4x4(&matrices[index]) *
                    DirectX::XMLoadFloat4x4(&matrices[*parents[index]]));
                if (!SceneRuntime::SceneTransforms::IsUsable(matrices[index]))
                    throw std::runtime_error("Inherited transform is outside the supported range");
            }
            visited[index]=2;
        }
    }
}

namespace SceneRuntime
{
    bool SceneTransforms::IsUsable(const DirectX::XMFLOAT4X4& matrix)
    {
        if (!Finite(matrix) || std::abs(matrix._14)>1e-6f || std::abs(matrix._24)>1e-6f ||
            std::abs(matrix._34)>1e-6f || std::abs(matrix._44-1)>1e-6f) return false;
        DirectX::XMVECTOR determinant;
        Matrix inverse;
        DirectX::XMStoreFloat4x4(&inverse,DirectX::XMMatrixInverse(&determinant,DirectX::XMLoadFloat4x4(&matrix)));
        const auto value=DirectX::XMVectorGetX(determinant);
        return std::isfinite(value) && value!=0 && Finite(inverse);
    }
    bool SceneTransforms::Compose(const ScenePlacement& placement, DirectX::XMFLOAT4X4& output)
    {
        if (!std::all_of(placement.scale.begin(),placement.scale.end(),
            [](float value) { return std::isfinite(value) && std::abs(value)>=1e-6f; })) return false;
        using namespace DirectX;
        XMFLOAT4X4 candidate;
        XMStoreFloat4x4(&candidate,XMMatrixScaling(placement.scale[0],placement.scale[1],placement.scale[2]) *
            XMMatrixRotationX(placement.rotation[0]) * XMMatrixRotationY(placement.rotation[1]) *
            XMMatrixRotationZ(placement.rotation[2]) *
            XMMatrixTranslation(placement.position[0],placement.position[1],placement.position[2]));
        if (!IsUsable(candidate)) return false;
        output=candidate;
        return true;
    }
    bool SceneTransforms::WorldToLocal(const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& parentWorld, DirectX::XMFLOAT4X4& output)
    {
        if (!IsUsable(world) || !IsUsable(parentWorld)) return false;
        DirectX::XMFLOAT4X4 candidate;
        DirectX::XMStoreFloat4x4(&candidate,DirectX::XMLoadFloat4x4(&world) *
            DirectX::XMMatrixInverse(nullptr,DirectX::XMLoadFloat4x4(&parentWorld)));
        if (!IsUsable(candidate)) return false;
        output=candidate;
        return true;
    }
    bool SceneTransforms::Resolve(const SceneLayout& layout, TransformSpace space,
        std::vector<DirectX::XMFLOAT4X4>& output, std::string& error)
    {
        try
        {
            const auto parents=ParentIndices(layout);
            std::vector<DirectX::XMFLOAT4X4> matrices(layout.objects.size());
            for (size_t index=0;index<layout.objects.size();++index)
                if (!Compose(layout.objects[index],matrices[index])) throw std::runtime_error("Invalid transform: "+layout.objects[index].id);
            std::vector<unsigned char> visited(layout.objects.size(),0);
            for (size_t index=0;index<layout.objects.size();++index) ResolveChain(index,parents,space,visited,matrices);
            output=std::move(matrices);
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
}
