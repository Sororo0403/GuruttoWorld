#include <SceneRuntime/SceneTransforms.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <iterator>
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
    // Keep the existing mirror signs and choose the equivalent Euler angles nearest the inspector values.
    bool ReadSrt(const DirectX::XMFLOAT4X4& matrix, const SceneRuntime::ScenePlacement& reference,
        SceneRuntime::ScenePlacement& output)
    {
        auto result = reference;
        float r[3][3]{};
        for (int i=0; i<3; ++i)
        {
            const float length = std::sqrt(matrix.m[i][0]*matrix.m[i][0] +
                matrix.m[i][1]*matrix.m[i][1] + matrix.m[i][2]*matrix.m[i][2]);
            if (!std::isfinite(length) || length < 1e-6f) return false;
            result.scale[i] = std::copysign(length, reference.scale[i]);
            for (int j=0; j<3; ++j) r[i][j] = matrix.m[i][j] / result.scale[i];
        }
        const float y = std::asin(std::clamp(-r[0][2], -1.0f, 1.0f));
        float x, z;
        if (std::abs(std::cos(y)) > 1e-4f)
        {
            x=std::atan2(r[1][2],r[2][2]);
            z=std::atan2(r[0][1],r[0][0]);
        }
        else
        {
            z=reference.rotation[2];
            x=y>0 ? std::atan2(r[1][0],r[1][1])+z : std::atan2(-r[1][0],r[1][1])-z;
        }
        const auto nearest = [&](std::array<float,3> angles)
        {
            for (int i=0;i<3;++i) angles[i]=reference.rotation[i]+
                std::remainder(angles[i]-reference.rotation[i],DirectX::XM_2PI);
            return angles;
        };
        const auto primary=nearest({x,y,z});
        const auto alternate=nearest({x+DirectX::XM_PI,DirectX::XM_PI-y,z+DirectX::XM_PI});
        const auto distance = [&](const auto& angles)
        {
            float sum=0;
            for (int i=0;i<3;++i) { const float d=angles[i]-reference.rotation[i]; sum+=d*d; }
            return sum;
        };
        result.rotation=distance(primary)<=distance(alternate) ? primary : alternate;
        result.position={matrix._41,matrix._42,matrix._43};
        DirectX::XMFLOAT4X4 rebuilt;
        if (!SceneRuntime::SceneTransforms::Compose(result,rebuilt) ||
            !SceneRuntime::SceneTransforms::Matches(rebuilt,matrix)) return false;
        output=result;
        return true;
    }
    void ResolveChain(size_t start, const std::vector<std::optional<size_t>>& parents,
        std::vector<unsigned char>& visited, std::vector<Matrix>& matrices)
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
            if (parents[index])
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
    bool SceneTransforms::Matches(const DirectX::XMFLOAT4X4& left, const DirectX::XMFLOAT4X4& right)
    {
        if (!Finite(left) || !Finite(right)) return false;
        for (int row=0;row<4;++row)
            for (int column=0;column<4;++column)
                if (std::abs(left.m[row][column]-right.m[row][column])>
                    0.001f*std::max(1.0f,std::abs(right.m[row][column]))) return false;
        return true;
    }

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
    bool SceneTransforms::ReadTransform(const DirectX::XMFLOAT4X4& matrix,
        const ScenePlacement& reference, ScenePlacement& output)
    {
        if (!IsUsable(matrix)) return false;
        auto basis=reference;
        const auto determinant=DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(DirectX::XMLoadFloat4x4(&matrix)));
        const bool mirrored=std::signbit(basis.scale[0]) ^ std::signbit(basis.scale[1]) ^ std::signbit(basis.scale[2]);
        if (std::signbit(determinant)!=mirrored) basis.scale[0]=-basis.scale[0];
        return ReadSrt(matrix,basis,output);
    }

    bool SceneTransforms::Resolve(const SceneLayout& layout,
        std::vector<DirectX::XMFLOAT4X4>& output, std::string& error)
    {
        try
        {
            const auto parents=ParentIndices(layout);
            std::vector<DirectX::XMFLOAT4X4> matrices(layout.objects.size());
            for (size_t index=0;index<layout.objects.size();++index)
                if (!Compose(layout.objects[index],matrices[index])) throw std::runtime_error("Invalid transform: "+layout.objects[index].id);
            std::vector<unsigned char> visited(layout.objects.size(),0);
            for (size_t index=0;index<layout.objects.size();++index) ResolveChain(index,parents,visited,matrices);
            output=std::move(matrices);
            error.clear();
            return true;
        }
        catch (const std::exception& exception) { error=exception.what(); return false; }
    }
}
