#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "vertex.h"

struct STLVertex1;
constexpr int MAX_DYNAMIC_BATCH_VERTEX = 65536;
constexpr int MAX_DYNAMIC_BATCH_INDEX = 65536;

template <typename TVertex>
class CDynamicBatcher
{
    int meshesCount = 0;
    int indexesIndex = 0;
    int vertexesIndex = 0;
    std::unique_ptr<WORD[]> indexStore = std::make_unique<WORD[]>(MAX_DYNAMIC_BATCH_VERTEX);
    std::unique_ptr<TVertex[]> vertexStore = std::make_unique<TVertex[]>(MAX_DYNAMIC_BATCH_INDEX);

public:
    void Begin() noexcept
    {
        meshesCount = 0;
        indexesIndex = 0;
        vertexesIndex = 0;
    }

    void End() {}

    int AddVertices(const TVertex *pVertices, const DWORD dwVertexCount, const LPWORD pIndices,
                    const DWORD dwIndexCount)
    {
        if (vertexesIndex + dwVertexCount >= MAX_DYNAMIC_BATCH_VERTEX)
            return 0;

        if (indexesIndex + dwIndexCount >= MAX_DYNAMIC_BATCH_INDEX)
            return 0;

        memcpy(vertexStore.get() + vertexesIndex, pVertices, dwVertexCount * sizeof(TVertex));
        memcpy(indexStore.get() + indexesIndex, pIndices, dwIndexCount * sizeof(WORD));

        // remap indices to point at new indices
        WORD *idx = indexStore.get() + indexesIndex;
        for (int i = 0; i < dwIndexCount; i++)
        {
            idx[i] = pIndices[i] + vertexesIndex;
            assert(idx[i] < MAX_DYNAMIC_BATCH_INDEX);
        }
        vertexesIndex += dwVertexCount;
        indexesIndex += dwIndexCount;
        meshesCount++;

        return dwVertexCount;
    }

    void GetVertices(TVertex *&pVertices, DWORD &dwVertexCount, LPWORD &pIndices,
                     DWORD &dwIndexCount) noexcept
    {
        pVertices = vertexStore.get();
        dwVertexCount = vertexesIndex;
        pIndices = indexStore.get();
        dwIndexCount = indexesIndex;
    }
};

class CBatchers
{
    std::unique_ptr<CDynamicBatcher<STLVertex1>> m_batcher_stl =
        std::make_unique<CDynamicBatcher<STLVertex1>>();
    std::unique_ptr<CDynamicBatcher<PNTVertex>> m_batcher_pnt =
        std::make_unique<CDynamicBatcher<PNTVertex>>();

public:
    CDynamicBatcher<STLVertex1> *GetBatcher() { return m_batcher_stl.get(); }

    CDynamicBatcher<PNTVertex> *GetBatcher2() { return m_batcher_pnt.get(); }
};

// int CDX11::TriggerRedraw()
//{
//    RenderStateCheck();
//
//    std::shared_ptr<VertexBuffer<STLVertex1>> vertexBuf = m_caches->CreateOrUpdate(
//        vertexStore.get(), vertexesIndex);
//
//    this->deviceContext->IASetInputLayout(m_shaders->vs_simple.GetInputLayout());
//    this->deviceContext->VSSetShader(m_shaders->vs_simple.GetShader(), NULL, 0);
//    DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, vertexBuf.get(), indexStore.get(),
//                         indexesIndex);
//    pLastTexture = pBoundTexture;
//
//    indexesIndex = 0;
//    vertexesIndex = 0;
//    return 0;
//}
