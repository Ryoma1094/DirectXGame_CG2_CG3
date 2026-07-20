#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <vector>
#include <wincodec.h>
#include <cmath>
#include <fstream>  
#include <sstream>  
#include <iterator>
#include <wrl.h> 

// DXCコンパイルに必要なヘッダーとライブラリ
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")

// ReportLiveObjects用のヘッダーとライブラリ
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"Windowscodecs.lib")

#ifndef USE_IMGUI
#define USE_IMGUI
#endif
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 自動リークチェッカー構造体の定義
struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	}
};

// 文字列変換用ヘルパー関数（string -> wstring）
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) return L"";
	int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	if (sizeNeeded == 0) {
		sizeNeeded = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
		std::wstring wstr(sizeNeeded - 1, 0);
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], sizeNeeded);
		return wstr;
	}
	std::wstring wstr(sizeNeeded - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], sizeNeeded);
	return wstr;
}

// 頂点データ構造体
struct Vector4 { float x, y, z, w; };
struct Vector3 { float x, y, z; };
struct Vector2 { float u, v; };
struct Vertex {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct MaterialData {
	std::string textureFilePath;
};

struct ModelData {
	std::vector<Vertex> vertices;
	MaterialData material;
};

struct Matrix4x4 { float m[4][4]; };

struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix {
	Matrix4x4 wvp;
	Matrix4x4 world;
};
struct Transform {
	float scale[3];
	float rotate[3];
	float translate[3];
};

struct DirectionalLight {
	Vector4 color;
	Vector3 direction;
	float intensity;
};

// --- 行列計算・ヘルパー関数群 ---
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
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] +
				m1.m[i][1] * m2.m[1][j] +
				m1.m[i][2] * m2.m[2][j] +
				m1.m[i][3] * m2.m[3][j];
		}
	}
	return result;
}
Matrix4x4 MakeScaleMatrix(const float scale[3]) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = scale[0];
	result.m[1][1] = scale[1];
	result.m[2][2] = scale[2];
	return result;
}
Matrix4x4 MakeRotateZMatrix(float rotateZ) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = std::cos(rotateZ);  result.m[0][1] = std::sin(rotateZ);
	result.m[1][0] = -std::sin(rotateZ); result.m[1][1] = std::cos(rotateZ);
	return result;
}
Matrix4x4 MakeTranslateMatrix(const float translate[3]) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[3][0] = translate[0];
	result.m[3][1] = translate[1];
	result.m[3][2] = translate[2];
	return result;
}
Matrix4x4 MakeAffineMatrix(const float scale[3], const float rotate[3], const float translate[3]) {
	Matrix4x4 mScale = MakeScaleMatrix(scale);
	Matrix4x4 mRotX = MakeIdentity4x4();
	mRotX.m[1][1] = std::cos(rotate[0]); mRotX.m[1][2] = std::sin(rotate[0]);
	mRotX.m[2][1] = -std::sin(rotate[0]); mRotX.m[2][2] = std::cos(rotate[0]);
	Matrix4x4 mRotY = MakeIdentity4x4();
	mRotY.m[0][0] = std::cos(rotate[1]); mRotY.m[0][2] = -std::sin(rotate[1]);
	mRotY.m[2][0] = std::sin(rotate[1]); mRotY.m[2][2] = std::cos(rotate[1]);
	Matrix4x4 mRotZ = MakeRotateZMatrix(rotate[2]);
	Matrix4x4 mRotate = Multiply(Multiply(mRotX, mRotY), mRotZ);
	Matrix4x4 mTranslate = MakeTranslateMatrix(translate);
	return Multiply(Multiply(mScale, mRotate), mTranslate);
}
Matrix4x4 MakeUVTransformMatrix(const float scale[3], float rotateZ, const float translate[3]) {
	Matrix4x4 mScale = MakeIdentity4x4();
	mScale.m[0][0] = scale[0]; mScale.m[1][1] = scale[1];
	Matrix4x4 mRotZ = MakeRotateZMatrix(rotateZ);
	Matrix4x4 mTranslate = MakeIdentity4x4();
	mTranslate.m[3][0] = translate[0]; mTranslate.m[3][1] = translate[1];
	return Multiply(Multiply(mScale, mRotZ), mTranslate);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
#endif
	if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

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

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils,
	const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler,
	const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler) {

	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	DxcBuffer shaderSourceBuffer{ shaderSource->GetBufferPointer(), shaderSource->GetBufferSize(), DXC_CP_UTF8 };
	LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr", L"-HV", L"2021" };

	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler.Get(), IID_PPV_ARGS(&shaderResult));

	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError && shaderError->GetStringLength() != 0) { OutputDebugStringA(reinterpret_cast<const char*>(shaderError->GetStringPointer())); assert(false); }

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	return shaderBlob;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{ heapType, numDescriptors, shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}

// 🌟WICオブジェクトもComPtr化して自動解放に対応
void LoadTextureFile(const std::wstring& filePath, std::vector<uint8_t>& outData, D3D12_RESOURCE_DESC& outDesc) {
	Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory = nullptr;
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder = nullptr;
	HRESULT hr = wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	assert(SUCCEEDED(hr));
	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame = nullptr; decoder->GetFrame(0, &frame);
	UINT width = 0, height = 0; frame->GetSize(&width, &height);
	outData.resize(width * height * 4);
	frame->CopyPixels(nullptr, width * 4, static_cast<UINT>(outData.size()), outData.data());
	outDesc = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height) {
	D3D12_RESOURCE_DESC resourceDesc{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)width, (UINT)height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL };
	D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE depthClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, {1.0f, 0} };
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	return resource;
}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData;
	std::string line;
	std::ifstream file(directoryPath + "/" + filename);
	if (!file.is_open()) return materialData;

	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}
	return materialData;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> texcoords;
	std::string line;

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);
		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.u >> texcoord.v;
			texcoord.v = 1.0f - texcoord.v;
			texcoords.push_back(texcoord);
		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);
		} else if (identifier == "mtllib") {
			std::string materialFilename;
			s >> materialFilename;
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		} else if (identifier == "f") {
			Vertex triangle[3];

			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3] = { 0,0,0 };
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					if (std::getline(v, index, '/')) {
						elementIndices[element] = index.empty() ? 0 : std::stoi(index);
					}
				}

				Vector4 position = (elementIndices[0] > 0) ? positions[elementIndices[0] - 1] : Vector4{ 0,0,0,1 };
				Vector2 texcoord = (elementIndices[1] > 0) ? texcoords[elementIndices[1] - 1] : Vector2{ 0,0 };
				Vector3 normal = (elementIndices[2] > 0) ? normals[elementIndices[2] - 1] : Vector3{ 0,0,-1 };

				position.x *= -1.0f;
				normal.x *= -1.0f;

				triangle[faceVertex] = { position, texcoord, normal };
			}

			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		}
	}
	return modelData;
}

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	D3DResourceLeakChecker leakCheck;

	WNDCLASS wc{ 0, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"CG2WindowClass" };
	RegisterClass(&wc);
	const int32_t kClientWidth = 1280; const int32_t kClientHeight = 720;
	RECT wrc = { 0, 0, kClientWidth, kClientHeight }; AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);
	HWND hwnd = CreateWindow(wc.lpszClassName, L"CG2 - MultiTexture", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wrc.right - wrc.left, wrc.bottom - wrc.top, nullptr, nullptr, wc.hInstance, nullptr);
	ShowWindow(hwnd, SW_SHOW);

	{
		Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
#ifdef _DEBUG
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) { debugController->EnableDebugLayer(); debugController->SetEnableGPUBasedValidation(TRUE); }
#endif

		Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr; CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
		Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
		for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; i++) {
			DXGI_ADAPTER_DESC3 adapterDesc{}; useAdapter->GetDesc3(&adapterDesc);
			if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
			useAdapter = nullptr;
		}

		Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
		D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
		for (size_t i = 0; i < _countof(featureLevels); ++i) { if (SUCCEEDED(D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device)))) break; }

		const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		// 🌟修正：記述漏れしていた commandQueueDesc の定義を追加
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr; device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr; device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr; device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

		Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = (UINT)kClientWidth; swapChainDesc.Height = (UINT)kClientHeight;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1; swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH; swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; swapChainDesc.Flags = 0;

		dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

		Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = { nullptr }; swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0])); swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D12_RTV_DIMENSION_TEXTURE2D };

		D3D12_RENDER_TARGET_VIEW_DESC rtvDescSRGB = rtvDesc;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
		rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 0); device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDescSRGB, rtvHandles[0]);
		// 🌟修正：swapChainResources[1] に .Get() が抜けていたのを修正
		rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 1); device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDescSRGB, rtvHandles[1]);

		Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{ DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D, D3D12_DSV_FLAG_NONE };
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandleStart = GetCPUDescriptorHandle(dsvDescriptorHeap.Get(), descriptorSizeDSV, 0);
		device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandleStart);
		commandList->Close();

		Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr; uint64_t fenceValue = 0; device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

		Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr; Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr; Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)); DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)); dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

		D3D12_ROOT_PARAMETER rootParameters[4] = {};
		rootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[0].Descriptor.ShaderRegister = 0;
		rootParameters[1] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_VERTEX }; rootParameters[1].Descriptor.ShaderRegister = 1;
		D3D12_DESCRIPTOR_RANGE descriptorRange[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
		rootParameters[2] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[2].DescriptorTable = { 1, descriptorRange };
		rootParameters[3] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[3].Descriptor.ShaderRegister = 1;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = { {D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0, 0, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL} };

		D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{ _countof(rootParameters), rootParameters, _countof(staticSamplers), staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr; Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr; D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr; device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSignature.Get(); psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

		Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
		Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
		psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
		psoDesc.DepthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL }; psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; psoDesc.SampleDesc.Count = 1; psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState = nullptr; device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));

		D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };

		ModelData modelData = LoadObjFile("resources", "plane.obj");

		D3D12_RESOURCE_DESC vertexResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * modelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
		device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResource));

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
		vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * modelData.vertices.size());
		vertexBufferView.StrideInBytes = sizeof(Vertex);

		Vertex* vertexData = nullptr;
		vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
		std::memcpy(vertexData, modelData.vertices.data(), sizeof(Vertex) * modelData.vertices.size());
		vertexResource->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC vertexResourceDescSprite = vertexResourceDesc; vertexResourceDescSprite.Width = sizeof(Vertex) * 4;
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSprite));
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{ vertexResourceSprite->GetGPUVirtualAddress(), sizeof(Vertex) * 4, sizeof(Vertex) };
		Vertex* vertexDataSprite = nullptr; vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

		// スプライト頂点データ初期化
		Vector3 nSprite = { 0.0f, 0.0f, -1.0f };
		vertexDataSprite[0] = { {0.0f, 360.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, nSprite };
		vertexDataSprite[1] = { {0.0f, 0.0f, 0.0f, 1.0f},   {0.0f, 0.0f}, nSprite };
		vertexDataSprite[2] = { {640.0f, 360.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, nSprite };
		vertexDataSprite[3] = { {640.0f, 0.0f, 0.0f, 1.0f},   {1.0f, 0.0f}, nSprite };
		vertexResourceSprite->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC indexResourceDescSprite{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(uint32_t) * 6, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &indexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexResourceSprite));

		D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
		indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
		indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
		indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

		uint32_t* indexDataSprite = nullptr; indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
		indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
		indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;
		indexResourceSprite->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC materialResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(Material) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResource));
		Material* materialData = nullptr; materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
		materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; materialData->enableLighting = true;
		materialData->uvTransform = MakeIdentity4x4();

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResourceSprite));
		Material* materialDataSprite = nullptr; materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
		materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f }; materialDataSprite->enableLighting = false;
		materialDataSprite->uvTransform = MakeIdentity4x4();

		D3D12_RESOURCE_DESC wvpResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(TransformationMatrix) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&wvpResource));
		TransformationMatrix* wvpData = nullptr; wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
		wvpData->wvp = MakeIdentity4x4(); wvpData->world = MakeIdentity4x4();

		Transform transformModel{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

		Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformationMatrixResourceSprite));
		TransformationMatrix* transformationMatrixDataSprite = nullptr; transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
		transformationMatrixDataSprite->wvp = MakeIdentity4x4(); transformationMatrixDataSprite->world = MakeIdentity4x4();
		Transform transformSprite{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

		Transform uvTransformSprite{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

		D3D12_RESOURCE_DESC directionalLightResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(DirectionalLight) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &directionalLightResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&directionalLightResource));
		DirectionalLight* directionalLightData = nullptr; directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
		directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; directionalLightData->direction = { 0.0f, -1.0f, 0.0f }; directionalLightData->intensity = 1.0f;

		std::wstring modelTexturePath = L"Resources/uvChecker.png";
		if (!modelData.material.textureFilePath.empty()) {
			modelTexturePath = ConvertString(modelData.material.textureFilePath);
		}
		std::vector<uint8_t> textureData; D3D12_RESOURCE_DESC textureDesc{}; LoadTextureFile(modelTexturePath, textureData, textureDesc);
		D3D12_HEAP_PROPERTIES textureHeapProps{ D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0 };
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource));
		textureResource->WriteToSubresource(0, nullptr, textureData.data(), (UINT)(textureDesc.Width * 4), (UINT)textureData.size());

		std::vector<uint8_t> textureData2; D3D12_RESOURCE_DESC textureDesc2{}; LoadTextureFile(L"Resources/monsterBall.png", textureData2, textureDesc2);
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2 = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc2, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource2));
		textureResource2->WriteToSubresource(0, nullptr, textureData2.data(), (UINT)(textureDesc2.Width * 4), (UINT)textureData2.size());

		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU1 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{ textureDesc.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc1.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(textureResource.Get(), &srvDesc1, textureSrvHandleCPU1);

		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{ textureDesc2.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc2.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 1.0f };
		D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

#ifdef USE_IMGUI
		IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark(); ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX12_Init(device.Get(), 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvDescriptorHeap.Get(), GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0), GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0));
#endif

		float cameraTranslate[3] = { 0.0f, 4.0f, -10.0f };
		float cameraRotate[3] = { 0.296f, 0.0f, 0.0f };
		bool isAutoRotate = false; bool useMonsterBall = false;
		float pi_f = 3.1415926535f;

		bool drawSprite = false;

		MSG msg{};
		while (msg.message != WM_QUIT) {
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); } else {
				if (isAutoRotate) { transformModel.rotate[1] += 0.01f; if (transformModel.rotate[1] > pi_f * 2.0f) transformModel.rotate[1] -= pi_f * 2.0f; }

#ifdef USE_IMGUI
				ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
				ImGui::Begin("Settings");
				if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat3("CameraTranslate", cameraTranslate, 0.1f);
					ImGui::SliderFloat3("CameraRotate", cameraRotate, -pi_f, pi_f);
				}
				if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::ColorEdit4("color", &materialData->color.x); }
				if (ImGui::CollapsingHeader("TransformModel", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("useMonsterBall", &useMonsterBall); ImGui::Checkbox("Auto Rotate", &isAutoRotate); ImGui::SliderFloat3("rotate", transformModel.rotate, -pi_f, pi_f); }

				if (ImGui::CollapsingHeader("Sprite Options", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Draw Sprite", &drawSprite);
				}
				if (drawSprite && ImGui::CollapsingHeader("TransformSprite", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::SliderFloat2("scale", transformSprite.scale, 0.1f, 5.0f); ImGui::SliderFloat2("translate", transformSprite.translate, -200.0f, 1000.0f); }
				if (drawSprite && ImGui::CollapsingHeader("UV Transform Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat2("UVTranslate", uvTransformSprite.translate, 0.01f, -10.0f, 10.0f);
					ImGui::DragFloat2("UVScale", uvTransformSprite.scale, 0.01f, -10.0f, 10.0f);
					ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate[2]);
				}
				if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::SliderFloat3("Light Direction", &directionalLightData->direction.x, -1.0f, 1.0f); ImGui::ColorEdit4("Light Color", &directionalLightData->color.x); ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 5.0f); }
				ImGui::End(); ImGui::Render();
#endif

				Matrix4x4 materialTransformMatrix = MakeUVTransformMatrix(uvTransformSprite.scale, uvTransformSprite.rotate[2], uvTransformSprite.translate);
				materialDataSprite->uvTransform = materialTransformMatrix;

				float negTranslate[3] = { -cameraTranslate[0], -cameraTranslate[1], -cameraTranslate[2] };
				Matrix4x4 viewTranslateMatrix = MakeTranslateMatrix(negTranslate);

				Matrix4x4 viewRotateXMatrix = MakeIdentity4x4();
				viewRotateXMatrix.m[1][1] = std::cos(-cameraRotate[0]); viewRotateXMatrix.m[1][2] = std::sin(-cameraRotate[0]);
				viewRotateXMatrix.m[2][1] = -std::sin(-cameraRotate[0]); viewRotateXMatrix.m[2][2] = std::cos(-cameraRotate[0]);

				Matrix4x4 viewRotateYMatrix = MakeIdentity4x4();
				viewRotateYMatrix.m[0][0] = std::cos(-cameraRotate[1]); viewRotateYMatrix.m[0][2] = -std::sin(-cameraRotate[1]);
				viewRotateYMatrix.m[2][0] = std::sin(-cameraRotate[1]); viewRotateYMatrix.m[2][2] = std::cos(-cameraRotate[1]);

				Matrix4x4 viewRotateZMatrix = MakeRotateZMatrix(-cameraRotate[2]);

				Matrix4x4 viewMatrix = Multiply(Multiply(Multiply(viewTranslateMatrix, viewRotateZMatrix), viewRotateYMatrix), viewRotateXMatrix);

				Matrix4x4 worldMatrixModel = MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate);
				Matrix4x4 projectionMatrixModel = MakePerspectiveFovMatrix(0.45f, (float)kClientWidth / kClientHeight, 0.1f, 100.0f);
				wvpData->wvp = Multiply(Multiply(worldMatrixModel, viewMatrix), projectionMatrixModel);
				wvpData->world = worldMatrixModel;

				Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
				Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 100.0f);
				transformationMatrixDataSprite->wvp = Multiply(worldMatrixSprite, projectionMatrixSprite);
				transformationMatrixDataSprite->world = worldMatrixSprite;

				commandAllocator->Reset();
				commandList->Reset(commandAllocator.Get(), nullptr);
				UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
				commandList->ResourceBarrier(1, &barrier);

				ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
				commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

				D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap.Get(), descriptorSizeDSV, 0);
				commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

				float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
				commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
				commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

				commandList->RSSetViewports(1, &viewport);
				commandList->RSSetScissorRects(1, &scissorRect);
				commandList->SetGraphicsRootSignature(rootSignature.Get());
				commandList->SetPipelineState(pipelineState.Get());
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

				// 1. 3Dモデルの描画
				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
				commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU1);
				commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);

				// 2. 2Dスプライトの描画
				if (drawSprite) {
					commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
					commandList->IASetIndexBuffer(&indexBufferViewSprite);
					commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
					commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
				}

#ifdef USE_IMGUI
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
				commandList->ResourceBarrier(1, &barrier);

				commandList->Close();
				ID3D12CommandList* commandLists[] = { commandList.Get() };
				commandQueue->ExecuteCommandLists(1, commandLists);

				swapChain->Present(1, 0);
				fenceValue++;
				commandQueue->Signal(fence.Get(), fenceValue);
				if (fence->GetCompletedValue() < fenceValue) {
					fence->SetEventOnCompletion(fenceValue, fenceEvent);
					WaitForSingleObject(fenceEvent, INFINITE);
				}
			}
		}

#ifdef USE_IMGUI
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
#endif

		directionalLightResource->Unmap(0, nullptr);
		materialResourceSprite->Unmap(0, nullptr);
		transformationMatrixResourceSprite->Unmap(0, nullptr);
		wvpResource->Unmap(0, nullptr);
		materialResource->Unmap(0, nullptr);

		CloseHandle(fenceEvent);
	}

	UnregisterClass(wc.lpszClassName, wc.hInstance);
	CoUninitialize();
	return 0;
}