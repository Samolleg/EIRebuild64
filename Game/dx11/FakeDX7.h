#pragma once

class FakeD3D7State
{
    typedef std::map<int, DWORD> RenderStateMap;

public:
    RenderStateMap render_state;
    void SetRenderState(int key, DWORD value);
    void GetRenderState(int key, DWORD *value) { *value = render_state[key]; };
};
