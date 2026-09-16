
#include "stdafx.h"
#include "FakeDX7.h"
#include <sstream>

void FakeD3D7State::SetRenderState(int key, DWORD value)
{
    render_state[key] = value;
}