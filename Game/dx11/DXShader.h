#pragma once
#pragma comment(lib, "D3DCompiler.lib")
#include <d3d11.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
// #include <DirectXHelpers.h>

HRESULT WINAPI D3DReadFileToBlobS(_In_ std::string pFileName, _Out_ ID3DBlob **ppContents);


bool ReportError(HRESULT code, const std::string& format);

template <typename TShader>
void PrintShaderReflection(TShader *shader);

class VertexShader
{
public:
    bool Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device, const std::string& shaderpath,
                    D3D11_INPUT_ELEMENT_DESC *layoutDesc, UINT numElements);
    ID3D11VertexShader *GetShader();
    ID3DBlob *GetBuffer();
    ID3D11InputLayout *GetInputLayout();

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> shader_buffer;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
};

class PixelShader
{
public:
    bool Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device, const std::string& shaderpath);
    ID3D11PixelShader *GetShader();
    ID3DBlob *GetBuffer();

private:
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> shader_buffer;
};

class GeometryShader
{
public:
    bool Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device, const std::string& shaderpath);
    ID3D11GeometryShader *GetShader() { return this->shader.Get(); };
    ID3DBlob *GetBuffer() { return this->shader_buffer.Get(); };

private:
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> shader_buffer;
};

class CShadersMain
{
public:
    PixelShader ps_main;
    VertexShader vs_main;
    VertexShader vs_simple;
    GeometryShader gs_point_to_quad;

    bool Initialize(Microsoft::WRL::ComPtr<ID3D11Device> &device);
};