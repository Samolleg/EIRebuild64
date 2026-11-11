// The F10 overlay (Dear ImGui draw lists) on the D3D12 backend
// (Host/d3d12/d3d12_frame.cpp). Native program: register space 1, the
// texture read from the heap by the index in the root constants.

cbuffer Root : register(b0, space1) {   // the 8 root constants
    float2 scale;        // pixels to clip space
    float2 offset;
    uint textureIndex;   // Texture2D<float4> in ResourceDescriptorHeap
    uint3 rootPad;
};

SamplerState linearClamp : register(s2, space1);

struct OverlayIn {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct OverlayOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

OverlayOut imguiVS(OverlayIn i) {
    OverlayOut o;
    o.position = float4(i.position * scale + offset, 0, 1);
    o.uv = i.uv;
    o.color = i.color;
    return o;
}

float4 imguiPS(OverlayOut i) : SV_Target {
    Texture2D<float4> texture = ResourceDescriptorHeap[textureIndex];
    return i.color * texture.Sample(linearClamp, i.uv);
}
