#include "stdafx.h"
#include "DXShader.h"
#include "DirectXHelpers.h"

template <typename TShader>
void PrintShaderReflection(TShader *shader)
{
    ID3DBlob *buffer = shader->GetBuffer();
    ID3D11ShaderReflection *pReflector;
    IfFailedThrowHR(
        D3DReflect(buffer->GetBufferPointer(), buffer->GetBufferSize(), IID_PPV_ARGS(&pReflector)));
    D3D11_SHADER_DESC vbDesc;
    IfFailedThrowHR(pReflector->GetDesc(&vbDesc));

    auto describeBufferByIndex = [pReflector](int index) {
        ID3D11ShaderReflectionConstantBuffer *pConstBuff = pReflector->GetConstantBufferByIndex(
            index);
        D3D11_SHADER_BUFFER_DESC cbDesc;
        ZeroMemory(&cbDesc, sizeof(cbDesc));
        IfFailedThrowHR(pConstBuff->GetDesc(&cbDesc));

        std::vector<D3D11_SHADER_VARIABLE_DESC> variables;

        const int maxVars = min(100, cbDesc.Variables);

        for (int i = 0; i < maxVars; ++i)
        {
            auto var = pConstBuff->GetVariableByIndex(i);
            variables.emplace_back();
            var->GetDesc(&variables.back());
            char msg[256]{ 0 };
            D3D11_SHADER_VARIABLE_DESC varDesc = variables.back();
            snprintf(msg, 256, "VAR %s SIZE %u OFFSET %u\n", varDesc.Name, varDesc.Size,
                     varDesc.StartOffset);
            OutputDebugString(msg);
        }
    };

    char msg[256]{ 0 };
    snprintf(msg, 256, "SHADER REF. N BUFFERS %u\n", vbDesc.ConstantBuffers);
    OutputDebugString(msg);

    for (int i = 0; i < vbDesc.ConstantBuffers; i++)
    {
        char msg[256]{ 0 };
        snprintf(msg, 256, "BUFFER NO %u\n", i);
        OutputDebugString(msg);
        describeBufferByIndex(i);
    }
}

bool CShadersMain::Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device)
{
    std::string shaderfolder = ".\\shaders\\";
    if (IsDebuggerPresent() != FALSE)
    {
#pragma region DetermineShaderPath
        // wont work for optimized debug...
#ifdef _DEBUG
        std::string config = "debug";
#else
        std::string config = "release";
#endif
#ifdef _WIN64
        assert(0); // probably "x64", who cares
#else
        std::string platform = "Win32";
#endif
        shaderfolder += platform + "_" + config + "\\";
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SPECULAR", 0, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "USE_VERTEX_COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_R32_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    UINT numElements = ARRAYSIZE(layout);

    if (!vs_main.Initialize(device, shaderfolder + "vertexshader.cso", layout, numElements))
        return false;

    D3D11_INPUT_ELEMENT_DESC layout2[] = {
        { "POSITION", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SPECULAR", 0, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    numElements = ARRAYSIZE(layout2);

    if (!vs_simple.Initialize(device, shaderfolder + "vertexshader_simple.cso", layout2,
                              numElements))
        return false;

    D3D11_INPUT_ELEMENT_DESC layout3[] = {
        { "POSITION", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SPECULAR", 0, DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SCREEN_DX", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT,
          D3D11_INPUT_CLASSIFICATION::D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    numElements = ARRAYSIZE(layout3);

    if (!vs_particles.Initialize(device, shaderfolder + "vertexshader_particles.cso", layout3,
                                 numElements))
        return false;

    if (!ps_main.Initialize(device, shaderfolder + "pixelshader.cso"))
        return false;

    PrintShaderReflection(&vs_main);
    PrintShaderReflection(&ps_main);

    return true;
}

HRESULT WINAPI D3DReadFileToBlobS(_In_ std::string pFileName, _Out_ ID3DBlob **ppContents)
{
    std::wstring stemp = std::wstring(pFileName.begin(), pFileName.end());
    LPCWSTR sw = stemp.c_str();
    return D3DReadFileToBlob(sw, ppContents);
}

bool ReportError(HRESULT code, const std::string& format)
{
    throw code;
    return ReportError(code, format.c_str());
}

std::string ExePath()
{
    TCHAR buffer[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, buffer, MAX_PATH);
    return std::string(buffer);
}

bool VertexShader::Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device, const std::string &shaderpath,
                              D3D11_INPUT_ELEMENT_DESC *layoutDesc, UINT numElements)
{
    // Debugging paths...ouch
    // std::string path = ExePath();
    // std::ifstream infile("..\\vertexshader.cso");
    // bool god = infile.good();

    // char curpath[120];
    // GetCurrentDirectory(120, (LPSTR)curpath);
    // shaderpath = curpath + shaderpath;

    HRESULT hr = D3DReadFileToBlobS(shaderpath, this->shader_buffer.GetAddressOf());
    if (FAILED(hr))
    {
        // bad pathing I guess
        std::string errorMsg = "Failed to load shader: ";
        errorMsg += shaderpath;
        ReportError(hr, errorMsg);
        return false;
    }

    hr = device->CreateVertexShader(this->shader_buffer->GetBufferPointer(),
                                    this->shader_buffer->GetBufferSize(), NULL,
                                    this->shader.GetAddressOf());
    if (FAILED(hr))
    {
        std::string errorMsg = "Failed to create vertex shader: ";
        errorMsg += shaderpath;
        ReportError(hr, errorMsg);
        return false;
    }

    hr = device->CreateInputLayout(layoutDesc, numElements, this->shader_buffer->GetBufferPointer(),
                                   this->shader_buffer->GetBufferSize(),
                                   this->inputLayout.GetAddressOf());
    if (FAILED(hr))
    {
        ReportError(hr, "Failed to create input layout.");
        return false;
    }

    DirectX::SetDebugObjectName(this->shader.Get(), "vertex_shader");

    return true;
}

ID3D11VertexShader *VertexShader::GetShader()
{
    return this->shader.Get();
}

ID3D10Blob *VertexShader::GetBuffer()
{
    return this->shader_buffer.Get();
}

ID3D11InputLayout *VertexShader::GetInputLayout()
{
    return this->inputLayout.Get();
}

bool PixelShader::Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device, const std::string& shaderpath)
{
    HRESULT hr = D3DReadFileToBlobS(shaderpath, this->shader_buffer.GetAddressOf());
    if (FAILED(hr))
    {
        std::string errorMsg = "Failed to load shader: ";
        errorMsg += shaderpath;
        ReportError(hr, errorMsg);
        return false;
    }

    hr = device->CreatePixelShader(this->shader_buffer.Get()->GetBufferPointer(),
                                   this->shader_buffer.Get()->GetBufferSize(), NULL,
                                   this->shader.GetAddressOf());
    if (FAILED(hr))
    {
        std::string errorMsg = "Failed to create pixel shader: ";
        errorMsg += shaderpath;
        ReportError(hr, errorMsg);
        return false;
    }

    DirectX::SetDebugObjectName(this->shader.Get(), "pixel_shader");

    return true;
}

ID3D11PixelShader *PixelShader::GetShader()
{
    return this->shader.Get();
}

ID3D10Blob *PixelShader::GetBuffer()
{
    return this->shader_buffer.Get();
}

bool GeometryShader::Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device, const std::string& shaderpath)
{
    HRESULT hr = D3DReadFileToBlobS(shaderpath, this->shader_buffer.GetAddressOf());
    if (FAILED(hr))
    {
        std::string errorMsg = "Failed to load shader: ";
        errorMsg += shaderpath;
        ReportError(hr, errorMsg);
        return false;
    }

    hr = device->CreateGeometryShader(this->shader_buffer.Get()->GetBufferPointer(),
                                      this->shader_buffer.Get()->GetBufferSize(), NULL,
                                      this->shader.GetAddressOf());
    if (FAILED(hr))
    {
        std::string errorMsg = "Failed to create vertex shader: ";
        errorMsg += shaderpath;
        ReportError(hr, errorMsg);
        return false;
    }

    return true;
}
