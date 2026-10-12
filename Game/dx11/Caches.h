#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <typeindex>
#include "Vertex.h"
#include "VertexBuffer.h"

class IVertexBufferCache
{
public:
    virtual ~IVertexBufferCache() = default;
    virtual std::shared_ptr<void> GetCacheObject(int key) = 0;
    virtual void SetCacheObject(int key, std::shared_ptr<void> value) = 0;
};

template <typename T>
class CSharedCache : public IVertexBufferCache
{
    std::unordered_map<int, std::shared_ptr<T>> m_cache;

public:
    std::shared_ptr<void> GetCacheObject(int key) override
    {
        auto it = m_cache.find(key);
        return (it != m_cache.end()) ? it->second : nullptr;
    }

    void SetCacheObject(int key, std::shared_ptr<void> value) override
    {
        m_cache[key] = std::static_pointer_cast<T>(value);
    }
};

inline int nearestPowerOfTwo(int number)
{
    if (number < 4)
    {
        return 4;
    }

    int power = std::ceil(std::log2(number));
    return static_cast<int>(std::pow(2, power));
}

class CVertexCaches
{
    std::map<std::type_index, EVertexType> vertexTypes;
    std::map<EVertexType, std::unique_ptr<IVertexBufferCache>> cacheMap;

    ID3D11Device *m_device;
    ID3D11DeviceContext *m_dc;

public:
    CVertexCaches(ID3D11Device *device, ID3D11DeviceContext *dc)
    {
        m_device = device;
        m_dc = dc;

        vertexTypes[typeid(STLVertex1)] = EVertexType::STL;
        vertexTypes[typeid(PNTVertex)] = EVertexType::PNT;
        vertexTypes[typeid(WORD)] = EVertexType::Word;

        cacheMap[EVertexType::STL] = std::make_unique<CSharedCache<VertexBuffer<STLVertex1>>>();
        cacheMap[EVertexType::PNT] = std::make_unique<CSharedCache<VertexBuffer<PNTVertex>>>();
        cacheMap[EVertexType::Word] = std::make_unique<CSharedCache<VertexBuffer<WORD>>>();
    }
    template <typename TVertex>
    std::shared_ptr<VertexBuffer<TVertex>> CreateOrUpdate(TVertex *src, DWORD dwItemCount)
    {
        std::shared_ptr<VertexBuffer<TVertex>> result;
        const int cache_key = nearestPowerOfTwo(dwItemCount);
        const EVertexType vertexType = vertexTypes[typeid(TVertex)];
        auto &cache = cacheMap[vertexType];
        std::shared_ptr <void> cachedObj = cache->GetCacheObject(cache_key);
        if (!cachedObj)
        {
            result = std::make_shared<VertexBuffer<TVertex>>();
            IfFailedThrowHR(result->Initialize(m_device, static_cast<TVertex *>(src), dwItemCount, cache_key));
            cache->SetCacheObject(cache_key, std::static_pointer_cast<void>(result));
        }
        else
        {
            result = std::static_pointer_cast<VertexBuffer<TVertex>>(cachedObj);
            result->Update(m_dc, static_cast<void *>(src), dwItemCount);
        }
        return result;
    }
};