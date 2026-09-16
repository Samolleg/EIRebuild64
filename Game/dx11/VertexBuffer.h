#pragma once
#include <d3d11.h>
#include <wrl/client.h>

template <class T>
class VertexBuffer
{
private:
    VertexBuffer(const VertexBuffer<T> &rhs);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    std::unique_ptr<UINT> stride; // yes, that's a pointer to UINT for some reason...
    UINT bufferSize = 0;

public:
    VertexBuffer() {}

    ID3D11Buffer *Get() const { return buffer.Get(); }

    ID3D11Buffer *const *GetAddressOf() const { return buffer.GetAddressOf(); }

    UINT BufferSize() const { return this->bufferSize; }

    const UINT Stride() const { return *this->stride.get(); }

    const UINT *StridePtr() const { return this->stride.get(); }

    HRESULT Initialize(ID3D11Device *device, T *data, UINT numVertices, UINT paddedVertices);
    // update data from src count * sizeof(T)
    void Update(ID3D11DeviceContext *dc, void *src, int count);
};
