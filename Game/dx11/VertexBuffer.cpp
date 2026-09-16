#include "stdafx.h"
#include "VertexBuffer.h"
#include "Vertex.h"

template class VertexBuffer<PNTVertex>;
template class VertexBuffer<ParticleVertex>;
template class VertexBuffer<STLVertex1>;
template class VertexBuffer<WORD>;

template <class T>
HRESULT VertexBuffer<T>::Initialize(ID3D11Device *device, T *data, UINT numVertices, UINT paddedVertices)
{
    this->bufferSize = paddedVertices;
    this->stride = std::make_unique<UINT>(sizeof(T));

    D3D11_BUFFER_DESC vertexBufferDesc;
    ZeroMemory(&vertexBufferDesc, sizeof(vertexBufferDesc));

    vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    //vertexBufferDesc.Usage = D3D11_USAGE::D3D11_USAGE_IMMUTABLE;
    UINT desiredSize = sizeof(T) * paddedVertices; //create for paddedVerts
    UINT paddedSize = static_cast<UINT>(desiredSize + (16 - (desiredSize % 16))); // further align to 16 bytes
    vertexBufferDesc.ByteWidth = paddedSize;    
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    if (sizeof(T) == sizeof(WORD)) // haha! funny crutch for idx buffer
    vertexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    //vertexBufferDesc.CPUAccessFlags = 0;
    vertexBufferDesc.MiscFlags = 0;

    // pad data to fit into numVertices
    std::unique_ptr<char[]> tempBuf = std::make_unique<char[]>(paddedSize);
    ZeroMemory(tempBuf.get(), paddedSize);
    memcpy(tempBuf.get(), data, sizeof(T) * numVertices);

    D3D11_SUBRESOURCE_DATA vertexBufferData;
    ZeroMemory(&vertexBufferData, sizeof(vertexBufferData));
    vertexBufferData.pSysMem = tempBuf.get();

    this->buffer.Reset();
    HRESULT hr = device->CreateBuffer(&vertexBufferDesc, &vertexBufferData,
                                      this->buffer.GetAddressOf());

    return hr;
}

template <class T>
void VertexBuffer<T>::Update(ID3D11DeviceContext* dc, void *src, int count) // update count vertices
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    dc->Map(this->buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    //ZeroMemory(mapped.pData, this->bufferSize * sizeof(T));
    memcpy(mapped.pData, src, count * sizeof(T));
    dc->Unmap(this->buffer.Get(), 0);
}
