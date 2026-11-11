// D3D12 core bring-up frame (EI_RENDERER_X64_D3D12_CORE=1): a compute pass
// turns the last frame's per-pass GPU times into bars, a graphics pass draws
// the status text (GDI-rendered, uploaded through the ring) and the bars.
// Everything is read through ResourceDescriptorHeap with indices from the
// global root signature's constants (SM 6.6 bindless, plan 2.2/2.3).

// Native programs bind in register space 1 (space 0 is the D3D11 register
// model of the shared renderer HLSL; d3d12_core.h RootSlot).
cbuffer Root : register(b0, space1) {   // the 8 root constants
    uint barsUav;     // RWStructuredBuffer<Bar>
    uint barsSrv;     // StructuredBuffer<Bar>
    uint valuesSrv;   // StructuredBuffer<float>: pass milliseconds (transient)
    uint textSrv;     // Texture2D<float4>: the status text
    uint barCount;
    uint3 rootPad;
};

cbuffer Frame : register(b1, space1) {
    float2 viewport;    // pixels
    float2 textOrigin;  // pixels
    float2 textSize;    // pixels
    float barX;         // pixels
    float barY0;        // first bar's top, pixels
    float rowHeight;    // pixels
    float pxPerMs;
    float barMaxWidth;  // pixels
    float framePad;
};

SamplerState pointClamp : register(s0, space1);

struct Bar {
    float4 rect;   // x0, y0, x1, y1 in pixels
    float4 color;
};

[numthreads(64, 1, 1)]
void barsCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= barCount) return;
    StructuredBuffer<float> values = ResourceDescriptorHeap[valuesSrv];
    RWStructuredBuffer<Bar> bars = ResourceDescriptorHeap[barsUav];
    const float ms = values[id.x];
    const float y = barY0 + id.x * rowHeight;
    Bar bar;
    bar.rect = float4(barX, y + 3, barX + max(2.0, min(ms * pxPerMs, barMaxWidth)), y + rowHeight - 3);
    const float t = id.x / max(1.0, float(barCount - 1));
    bar.color = float4(lerp(float3(0.3, 0.9, 0.4), float3(1.0, 0.6, 0.2), t), 1);
    bars[id.x] = bar;
}

struct Varyings {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint text : TEXCOORD1;
};

// Instance 0: the text quad; instance i > 0: bar i - 1.
Varyings overlayVS(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float2 corner = float2((vertex == 1 || vertex == 4 || vertex == 5) ? 1 : 0, (vertex == 2 || vertex == 3 || vertex == 5) ? 1 : 0);
    float4 rect;
    Varyings o;
    if (instance == 0) {
        rect = float4(textOrigin, textOrigin + textSize);
        o.color = float4(1, 1, 1, 1);
        o.text = 1;
    } else {
        StructuredBuffer<Bar> bars = ResourceDescriptorHeap[barsSrv];
        const Bar bar = bars[instance - 1];
        rect = bar.rect;
        o.color = bar.color;
        o.text = 0;
    }
    const float2 pixel = lerp(rect.xy, rect.zw, corner);
    o.position = float4(pixel / viewport * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = corner;
    return o;
}

float4 overlayPS(Varyings v) : SV_Target {
    if (v.text == 0) return v.color;
    Texture2D<float4> text = ResourceDescriptorHeap[textSrv];
    const float4 texel = text.SampleLevel(pointClamp, v.uv, 0);
    // White text on black from GDI: coverage is the brightest channel.
    const float coverage = max(texel.r, max(texel.g, texel.b));
    return float4(texel.rgb, coverage);
}
