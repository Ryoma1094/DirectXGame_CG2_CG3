#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <vector>
#include <wincodec.h> // WIC用のヘッダー
#include <cmath>     // 回転・数学計算用

// DXCコンパイルに必要なヘッダーとライブラリ
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

// ReportLiveObjects用のヘッダーとライブラリ
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"Windowscodecs.lib") // WIC用のライブラリ

// ImGuiを有効化するフラグとインクルード設定
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// 頂点データ構造体
struct Vector4 { float x, y, z, w; };
struct Vector3 { float x, y, z; }; // 追加：法線用
struct Vector2 { float u, v; };

struct Vertex {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal; // 【拡張】スライド5ページ：法線情報の追加
};

// マテリアル・行列用構造体
struct Material {
	Vector4 color;
	int32_t enableLighting; // 【拡張】スライド8ページ：Lighting有効フラグ
	float padding[3];       // 16バイトアライメント（定数バッファ用）
};

struct Matrix4x4 { float m[4][4]; };

struct TransformationMatrix {
	Matrix4x4 wvp;
	Matrix4x4 world; // 【拡張】スライド11ページ：法線変換用のWorld行列を追加
};

struct Transform {
	float scale[3];
	float rotate[3];
	float translate[3];
};

// 平行光源用の構造体定義
struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
};

// --- 行列計算用のヘルパー関数群 ---
Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result = {};
	for (int i = 0; i < 4; ++i) result.m[i][i] = 1.0f;
	return result;
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspect, float nearClip, float farClip) {
	Matrix4x4 result = {};
	float h = 1.0f / std::tan(fovY / 2.0f);
	float w = h / aspect;
	result.m[0][0] = w;
	result.m[1][1] = h;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);
	return result;
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result = {};
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;
	return result;
}

Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result = {};
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] +
				m1.m[i][1] * m2.m[1][j] +
				m1.m[i][2] * m2.m[2][j] +
				m1.m[i][3] * m2.m[3][j];
		}
	}
	return result;
}

Matrix4x4 MakeAffineMatrix(const float scale[3], const float rotate[3], const float translate[3]) {
	Matrix4x4 mScale = MakeIdentity4x4();
	mScale.m[0][0] = scale[0]; mScale.m[1][1] = scale[1]; mScale.m[2][2] = scale[2];
	Matrix4x4 mRotX = MakeIdentity4x4();
	mRotX.m[1][1] = std::cos(rotate[0]); mRotX.m[1][2] = std::sin(rotate[0]);
	mRotX.m[2][1] = -std::sin(rotate[0]); mRotX.m[2][2] = std::cos(rotate[0]);
	Matrix4x4 mRotY = MakeIdentity4x4();
	mRotY.m[0][0] = std::cos(rotate[1]); mRotY.m[0][2] = -std::sin(rotate[1]);
	mRotY.m[2][0] = std::sin(rotate[1]); mRotY.m[2][2] = std::cos(rotate[1]);
	Matrix4x4 mRotZ = MakeIdentity4x4();
	mRotZ.m[0][0] = std::cos(rotate[2]); mRotZ.m[0][1] = std::sin(rotate[2]);
	mRotZ.m[1][0] = -std::sin(rotate[2]); mRotZ.m[1][1] = std::cos(rotate[2]);
	Matrix4x4 mRotate = Multiply(Multiply(mRotX, mRotY), mRotZ);
	Matrix4x4 mTranslate = MakeIdentity4x4();
	mTranslate.m[3][0] = translate[0]; mTranslate.m[3][1] = translate[1]; mTranslate.m[3][2] = translate[2];
	return Multiply(Multiply(mScale, mRotate), mTranslate);
}

// WindowProc
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
#endif
	if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// ログ出力用
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) return std::wstring();
	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}
void Log(const std::string& message) { OutputDebugStringA(message.c_str()); }
void Log(const std::wstring& message) { OutputDebugStringW(message.c_str()); }

// --- DescriptorHandle取得ヘルパー ---
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

// Shaderコンパイル
IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler) {
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	assert(SUCCEEDED(hr));
	DxcBuffer shaderSourceBuffer{ shaderSource->GetBufferPointer(), shaderSource->GetBufferSize(), DXC_CP_UTF8 };
	LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr", L"-HV", L"2021" };
	IDxcResult* shaderResult = nullptr;
	dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError && shaderError->GetStringLength() != 0) { Log(reinterpret_cast<const char*>(shaderError->GetStringPointer())); assert(false); }
	IDxcBlob* shaderBlob = nullptr;
	shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	shaderSource->Release(); shaderResult->Release();
	return shaderBlob;
}

// DescriptorHeap作成用
ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	ID3D12DescriptorHeap* descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{ heapType, numDescriptors, shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}

// テクスチャロード用
void LoadTextureFile(const std::wstring& filePath, std::vector<uint8_t>& outData, D3D12_RESOURCE_DESC& outDesc) {
	IWICImagingFactory* wicFactory = nullptr;
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
	IWICBitmapDecoder* decoder = nullptr;
	HRESULT hr = wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	if (FAILED(hr)) { Log(L"❌ ファイルが開けません: " + filePath + L"\n"); assert(SUCCEEDED(hr)); }
	IWICBitmapFrameDecode* frame = nullptr; decoder->GetFrame(0, &frame);
	UINT width = 0, height = 0; frame->GetSize(&width, &height);
	outData.resize(width * height * 4);
	frame->CopyPixels(nullptr, width * 4, static_cast<UINT>(outData.size()), outData.data());
	outDesc = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
	frame->Release(); decoder->Release(); wicFactory->Release();
}

// 深度ステンシル用リソース作成
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
	D3D12_RESOURCE_DESC resourceDesc{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)width, (UINT)height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL };
	D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE depthClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, {1.0f, 0} };
	ID3D12Resource* resource = nullptr;
	device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	return resource;
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	WNDCLASS wc{ 0, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"CG2WindowClass" };
	RegisterClass(&wc);
	const int32_t kClientWidth = 1280; const int32_t kClientHeight = 720;
	RECT wrc = { 0, 0, kClientWidth, kClientHeight }; AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);
	HWND hwnd = CreateWindow(wc.lpszClassName, L"CG2 - MultiTexture", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wrc.right - wrc.left, wrc.bottom - wrc.top, nullptr, nullptr, wc.hInstance, nullptr);
	ShowWindow(hwnd, SW_SHOW);

	ID3D12Debug1* debugController = nullptr;
#ifdef _DEBUG
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) { debugController->EnableDebugLayer(); debugController->SetEnableGPUBasedValidation(TRUE); }
#endif

	IDXGIFactory7* dxgiFactory = nullptr; CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	IDXGIAdapter4* useAdapter = nullptr;
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; i++) {
		DXGI_ADAPTER_DESC3 adapterDesc{}; useAdapter->GetDesc3(&adapterDesc);
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
		useAdapter = nullptr;
	}

	ID3D12Device* device = nullptr;
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
	for (size_t i = 0; i < _countof(featureLevels); ++i) { if (SUCCEEDED(D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device)))) break; }

	// 各種記述子サイズを取得
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	ID3D12CommandQueue* commandQueue = nullptr; D3D12_COMMAND_QUEUE_DESC commandQueueDesc{}; device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	ID3D12CommandAllocator* commandAllocator = nullptr; device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	ID3D12GraphicsCommandList* commandList = nullptr; device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	// スワップチェーン変数の宣言
	IDXGISwapChain4* swapChain = nullptr;

	// スワップチェーンの設定（個別指定初期化）
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = (UINT)kClientWidth;
	swapChainDesc.Height = (UINT)kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapChainDesc.Flags = 0;

	dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));

	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	ID3D12Resource* swapChainResources[2] = { nullptr }; swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0])); swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D12_RTV_DIMENSION_TEXTURE2D };

	// 作成した関数を使って RTV の DescriptorHandle を取得
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 0);
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 1);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{ DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D, D3D12_DSV_FLAG_NONE };

	// 作成した関数を使って DSV の DescriptorHandle を取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandleStart = GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0);
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandleStart);
	commandList->Close();

	ID3D12Fence* fence = nullptr; uint64_t fenceValue = 0; device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	IDxcUtils* dxcUtils = nullptr; IDxcCompiler3* dxcCompiler = nullptr; IDxcIncludeHandler* includeHandler = nullptr;
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)); DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)); dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

	// 平行光源用の定数バッファを追加するためルートパラメータ数を 4に拡張
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[1] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_VERTEX }; rootParameters[1].Descriptor.ShaderRegister = 1;
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
	rootParameters[2] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[2].DescriptorTable = { 1, descriptorRange };
	rootParameters[3] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[3].Descriptor.ShaderRegister = 2;

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = { {D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0, 0, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL} };

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.NumParameters = _countof(rootParameters);
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* signatureBlob = nullptr; ID3DBlob* errorBlob = nullptr; D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	ID3D12RootSignature* rootSignature = nullptr; device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature; psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3D.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3D.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
	psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
	psoDesc.DepthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL }; psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; psoDesc.SampleDesc.Count = 1; psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	ID3D12PipelineState* pipelineState = nullptr; device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));

	D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };


	// ==========================================
	// ① 球体データ作成 (IndexBuffer化バージョン)
	// ==========================================
	const uint32_t kSubdivision = 16;
	const float pi = 3.1415926535f;

	// 重複する頂点を共有するため、頂点数は (分割数+1)の二乗になります
	const uint32_t kNumSphereVertices = (kSubdivision + 1) * (kSubdivision + 1);
	// インデックスの数は元の頂点数と同じ (分割数^2 * 6)
	const uint32_t kNumSphereIndices = kSubdivision * kSubdivision * 6;

	// [球体の頂点リソース]
	D3D12_RESOURCE_DESC vertexResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * kNumSphereVertices, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* vertexResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResource));
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{ vertexResource->GetGPUVirtualAddress(), (UINT)(sizeof(Vertex) * kNumSphereVertices), sizeof(Vertex) };

	Vertex* vertexData = nullptr; vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	const float kLonEvery = (pi * 2.0f) / kSubdivision; const float kLatEvery = pi / kSubdivision;

	// 頂点データの生成（重複なし）
	for (uint32_t latIndex = 0; latIndex <= kSubdivision; ++latIndex) {
		float lat = -pi / 2.0f + kLatEvery * latIndex;
		float v = 1.0f - ((float)latIndex / kSubdivision);
		for (uint32_t lonIndex = 0; lonIndex <= kSubdivision; ++lonIndex) {
			float lon = lonIndex * kLonEvery;
			float u = (float)lonIndex / kSubdivision;
			uint32_t index = latIndex * (kSubdivision + 1) + lonIndex;

			Vector4 pos = { std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon), 1.0f };
			vertexData[index] = { pos, {u, v}, {pos.x, pos.y, pos.z} }; // 法線は位置と同じ
		}
	}
	vertexResource->Unmap(0, nullptr);

	// [球体のインデックスリソース]
	D3D12_RESOURCE_DESC indexResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(uint32_t) * kNumSphereIndices, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* indexResourceSphere = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &indexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexResourceSphere));
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{ indexResourceSphere->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * kNumSphereIndices), DXGI_FORMAT_R32_UINT };

	uint32_t* indexDataSphere = nullptr; indexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSphere));
	uint32_t indexOffset = 0;
	// インデックスデータの生成
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			uint32_t p0 = latIndex * (kSubdivision + 1) + lonIndex;
			uint32_t p1 = latIndex * (kSubdivision + 1) + (lonIndex + 1);
			uint32_t p2 = (latIndex + 1) * (kSubdivision + 1) + lonIndex;
			uint32_t p3 = (latIndex + 1) * (kSubdivision + 1) + (lonIndex + 1);

			// 三角形1
			indexDataSphere[indexOffset++] = p0; indexDataSphere[indexOffset++] = p2; indexDataSphere[indexOffset++] = p1;
			// 三角形2
			indexDataSphere[indexOffset++] = p1; indexDataSphere[indexOffset++] = p2; indexDataSphere[indexOffset++] = p3;
		}
	}
	indexResourceSphere->Unmap(0, nullptr);


	// ==========================================
	// ② Spriteデータ作成 (IndexBuffer化バージョン)
	// ==========================================

	// [Spriteの頂点リソース] 6頂点から4頂点へ節約！
	D3D12_RESOURCE_DESC vertexResourceDescSprite = vertexResourceDesc;
	vertexResourceDescSprite.Width = sizeof(Vertex) * 4;

	ID3D12Resource* vertexResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSprite));
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{ vertexResourceSprite->GetGPUVirtualAddress(), sizeof(Vertex) * 4, sizeof(Vertex) };

	Vertex* vertexDataSprite = nullptr; vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
	// Sprite用の4頂点のみ定義
	vertexDataSprite[0] = { {0.0f, 360.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f} }; // 左下
	vertexDataSprite[1] = { {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} };   // 左上
	vertexDataSprite[2] = { {640.0f, 360.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f} }; // 右下
	vertexDataSprite[3] = { {640.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f} };   // 右上
	vertexResourceSprite->Unmap(0, nullptr);

	// [Spriteのインデックスリソース] 
	D3D12_RESOURCE_DESC indexResourceDescSprite{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(uint32_t) * 6, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* indexResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &indexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexResourceSprite));
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{ indexResourceSprite->GetGPUVirtualAddress(), sizeof(uint32_t) * 6, DXGI_FORMAT_R32_UINT };

	uint32_t* indexDataSprite = nullptr; indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	// 4つの頂点を組み合わせて6頂点分（2つの三角形）を描画するインデックス
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;
	indexResourceSprite->Unmap(0, nullptr);


	// 3D用のマテリアル設定
	D3D12_RESOURCE_DESC materialResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(Material) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* materialResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResource));
	Material* materialData = nullptr; materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData->enableLighting = 1; // 球体はライティング有効

	// Sprite用のマテリアルを作成し、ライティングを無効化
	ID3D12Resource* materialResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResourceSprite));
	Material* materialDataSprite = nullptr; materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialDataSprite->enableLighting = 0; // Spriteはライティング無効

	D3D12_RESOURCE_DESC wvpResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(TransformationMatrix) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* wvpResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&wvpResource));
	TransformationMatrix* wvpData = nullptr; wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	wvpData->wvp = MakeIdentity4x4();
	wvpData->world = MakeIdentity4x4();
	Transform transformSphere{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	ID3D12Resource* transformationMatrixResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformationMatrixResourceSprite));
	TransformationMatrix* transformationMatrixDataSprite = nullptr; transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	transformationMatrixDataSprite->wvp = MakeIdentity4x4();
	transformationMatrixDataSprite->world = MakeIdentity4x4();
	Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	// 平行光源用の定数バッファ作成とデータ転送
	D3D12_RESOURCE_DESC directionalLightResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(DirectionalLight) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
	ID3D12Resource* directionalLightResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &directionalLightResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&directionalLightResource));
	DirectionalLight* directionalLightData = nullptr; directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };      // 白い光
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };  // 真上から下向きの光
	directionalLightData->intensity = 1.0f;                   // 輝度 1.0

	// --- 1枚目のテクスチャロード (uvChecker.png) ---
	std::vector<uint8_t> textureData; D3D12_RESOURCE_DESC textureDesc{}; LoadTextureFile(L"Resources/uvChecker.png", textureData, textureDesc);
	D3D12_HEAP_PROPERTIES textureHeapProps{ D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0 };
	ID3D12Resource* textureResource = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource));
	textureResource->WriteToSubresource(0, nullptr, textureData.data(), (UINT)(textureDesc.Width * 4), (UINT)textureData.size());

	// --- 2枚目のテクスチャロード (monsterBall.png) ---
	std::vector<uint8_t> textureData2; D3D12_RESOURCE_DESC textureDesc2{}; LoadTextureFile(L"Resources/monsterBall.png", textureData2, textureDesc2);
	ID3D12Resource* textureResource2 = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc2, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource2));
	textureResource2->WriteToSubresource(0, nullptr, textureData2.data(), (UINT)(textureDesc2.Width * 4), (UINT)textureData2.size());

	// --- DescriptorHandleの割り当て ---
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU1 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{ textureDesc.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc1.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureResource, &srvDesc1, textureSrvHandleCPU1);

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{ textureDesc2.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc2.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);

	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 1.0f };
	D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark(); ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device, 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvDescriptorHeap, GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0), GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0));
#endif

	float cameraZ = -13.0f; bool isAutoRotate = true;
	bool useMonsterBall = true;

	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); } else {
			if (isAutoRotate) { transformSphere.rotate[1] += 0.01f; if (transformSphere.rotate[1] > pi * 2.0f) transformSphere.rotate[1] -= pi * 2.0f; }

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
			ImGui::Begin("Settings");
			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::ColorEdit4("color", &materialData->color.x); }
			if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::SliderFloat("Camera Z", &cameraZ, -20.0f, -1.0f); }
			if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("Light Color", &directionalLightData->color.x);
				ImGui::SliderFloat3("Light Direction", &directionalLightData->direction.x, -1.0f, 1.0f);
				ImGui::SliderFloat("Light Intensity", &directionalLightData->intensity, 0.0f, 5.0f);
				float len = std::sqrt(directionalLightData->direction.x * directionalLightData->direction.x + directionalLightData->direction.y * directionalLightData->direction.y + directionalLightData->direction.z * directionalLightData->direction.z);
				if (len > 0.0f) { directionalLightData->direction.x /= len; directionalLightData->direction.y /= len; directionalLightData->direction.z /= len; }
			}

			if (ImGui::CollapsingHeader("TransformSphere", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("useMonsterBall", &useMonsterBall);
				ImGui::Checkbox("Auto Rotate", &isAutoRotate);
				ImGui::SliderFloat3("rotate", transformSphere.rotate, -pi, pi);
			}

			if (ImGui::CollapsingHeader("TransformSprite", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::SliderFloat2("scale", transformSprite.scale, 0.1f, 5.0f); ImGui::SliderFloat2("translate", transformSprite.translate, -200.0f, 1000.0f); }
			ImGui::End(); ImGui::Render();
#endif

			Matrix4x4 worldMatrixSphere = MakeAffineMatrix(transformSphere.scale, transformSphere.rotate, transformSphere.translate);
			Matrix4x4 viewMatrixSphere = MakeIdentity4x4(); viewMatrixSphere.m[3][2] = -cameraZ;
			Matrix4x4 projectionMatrixSphere = MakePerspectiveFovMatrix(0.45f, (float)kClientWidth / kClientHeight, 0.1f, 100.0f);
			wvpData->wvp = Multiply(Multiply(worldMatrixSphere, viewMatrixSphere), projectionMatrixSphere);
			wvpData->world = worldMatrixSphere;

			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 100.0f);
			transformationMatrixDataSprite->wvp = Multiply(worldMatrixSprite, projectionMatrixSprite);
			transformationMatrixDataSprite->world = worldMatrixSprite;

			commandAllocator->Reset(); commandList->Reset(commandAllocator, nullptr);
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			commandList->ResourceBarrier(1, &barrier);

			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap }; commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0);
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			commandList->RSSetViewports(1, &viewport); commandList->RSSetScissorRects(1, &scissorRect);
			commandList->SetGraphicsRootSignature(rootSignature); commandList->SetPipelineState(pipelineState);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// 平行光源データをバインド
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

			// ============================
			// 1. 3D球体の描画 (Index使用)
			// ============================
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->IASetIndexBuffer(&indexBufferViewSphere); // ★ 追加：IBVのセット
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU1);
			// ★ 変更：DrawInstanced から DrawIndexedInstanced へ
			commandList->DrawIndexedInstanced(kNumSphereIndices, 1, 0, 0, 0);

			// ============================
			// 2. 2Dスプライトの描画 (Index使用)
			// ============================
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			commandList->IASetIndexBuffer(&indexBufferViewSprite); // ★ 追加：IBVのセット
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
			// ★ 変更：DrawInstanced(6,1,0,0) から DrawIndexedInstanced(6,1,0,0,0) へ
			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);
			commandList->Close();
			ID3D12CommandList* commandLists[] = { commandList }; commandQueue->ExecuteCommandLists(1, commandLists);

			swapChain->Present(1, 0);
			fenceValue++; commandQueue->Signal(fence, fenceValue);
			if (fence->GetCompletedValue() < fenceValue) { fence->SetEventOnCompletion(fenceValue, fenceEvent); WaitForSingleObject(fenceEvent, INFINITE); }
		}
	}

#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
#endif

	// 後処理・アンマップ
	if (transformationMatrixResourceSprite) transformationMatrixResourceSprite->Unmap(0, nullptr);
	if (wvpResource) wvpResource->Unmap(0, nullptr);
	if (materialResource) materialResource->Unmap(0, nullptr);
	if (materialResourceSprite) materialResourceSprite->Unmap(0, nullptr);
	if (directionalLightResource) directionalLightResource->Unmap(0, nullptr);

	// リソース解放 (追加したインデックスリソースも忘れずに解放！)
	if (indexResourceSphere) indexResourceSphere->Release(); // ★ 追加
	if (indexResourceSprite) indexResourceSprite->Release(); // ★ 追加

	if (vertexResourceSprite) vertexResourceSprite->Release();
	if (transformationMatrixResourceSprite) transformationMatrixResourceSprite->Release();
	if (wvpResource) wvpResource->Release();
	if (materialResource) materialResource->Release();
	if (materialResourceSprite) materialResourceSprite->Release();
	if (directionalLightResource) directionalLightResource->Release();
	if (vertexResource) vertexResource->Release();
	if (textureResource) textureResource->Release();
	if (textureResource2) textureResource2->Release();
	if (depthStencilResource) depthStencilResource->Release();
	if (swapChainResources[0]) swapChainResources[0]->Release();
	if (swapChainResources[1]) swapChainResources[1]->Release();

	if (pipelineState) pipelineState->Release();
	if (vertexShaderBlob) vertexShaderBlob->Release();
	if (pixelShaderBlob) pixelShaderBlob->Release();
	if (rootSignature) rootSignature->Release();
	if (signatureBlob) signatureBlob->Release();
	if (errorBlob) errorBlob->Release();

	if (includeHandler) includeHandler->Release(); if (dxcCompiler) dxcCompiler->Release(); if (dxcUtils) dxcUtils->Release();
	if (fenceEvent) CloseHandle(fenceEvent); if (fence) fence->Release();

	if (rtvDescriptorHeap) rtvDescriptorHeap->Release();
	if (srvDescriptorHeap) srvDescriptorHeap->Release();
	if (dsvDescriptorHeap) dsvDescriptorHeap->Release();

	if (swapChain) swapChain->Release(); if (commandList) commandList->Release(); if (commandAllocator) commandAllocator->Release(); if (commandQueue) commandQueue->Release();
	if (device) device->Release(); if (useAdapter) useAdapter->Release(); if (dxgiFactory) dxgiFactory->Release();
#ifdef _DEBUG
	if (debugController != nullptr) debugController->Release();
#endif

	if (hwnd) DestroyWindow(hwnd);

#ifdef _DEBUG
	IDXGIDebug1* debug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) { debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL); debug->Release(); }
#endif
	CoUninitialize();
	return 0;
}