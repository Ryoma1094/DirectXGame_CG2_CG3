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

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#include <dxgidebug.h>

#include "Vector3.h"
#include "Matrix4x4.h"
#include "DebugCamera.h"

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

// ===================================================================
// 定数・列挙型定義
// ===================================================================
enum BlendMode {
	kBlendModeNone,     //!< ブレンドなし
	kBlendModeNormal,   //!< 通常αブレンド
	kBlendModeAdd,      //!< 加算
	kBlendModeSubtract, //!< 減算
	kBlendModeMultiply, //!< 乗算
	kBlendModeScreen,   //!< スクリーン
	kCountOfBlendMode
};

// ===================================================================
// 構造体定義
// ===================================================================
struct Vector2 { float x, y; };
struct Vector4 { float x, y, z, w; };
struct Vertex { Vector4 position; Vector2 texcoord; Vector3 normal; };

struct MaterialData { std::string textureFilePath; };
struct ModelData { std::vector<Vertex> vertices; MaterialData material; };

struct Material {
	Vector4 color;
	int32_t enableLighting;
	int32_t lightingType;
	int32_t enableAlphaDiscard;
	float discardThreshold;
	Matrix4x4 uvTransform;
};

// インスタンス用構造体 (HLSLの ParticleForGPU に対応)
struct ParticleForGPU {
	Matrix4x4 wvp;
	Matrix4x4 world;
	Vector4 color;
};

struct Transform {
	Vector3 scale{ 1.0f, 1.0f, 1.0f };
	Vector3 rotate{ 0.0f, 0.0f, 0.0f };
	Vector3 translate{ 0.0f, 0.0f, 0.0f };
};

struct ChunkHeader { char id[4]; int32_t size; };
struct RiffHeader { ChunkHeader chunk; char type[4]; };
struct FormatChunk { ChunkHeader chunk; WAVEFORMATEX fmt; };
struct SoundData { WAVEFORMATEX wfex; BYTE* pBuffer; unsigned int bufferSize; };

// 入力管理用
BYTE key[256] = {};
BYTE keyPre[256] = {};

// ===================================================================
// ユーティリティ・ヘルパー関数
// ===================================================================
bool IsKeyPressed(uint8_t keyCode) { return (key[keyCode] & 0x80) != 0; }
bool IsKeyReleased(uint8_t keyCode) { return (key[keyCode] & 0x80) == 0; }
bool IsKeyTriggered(uint8_t keyCode) { return ((key[keyCode] & 0x80) != 0) && ((keyPre[keyCode] & 0x80) == 0); }

struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	}
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
#endif
	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}
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

Matrix4x4 MakeUVTransformMatrix(const Vector3& scale, float rotateZ, const Vector3& translate) {
	Matrix4x4 mScale = Matrix4x4::MakeScaleMatrix({ scale.x, scale.y, 1.0f });
	Matrix4x4 mRotZ = Matrix4x4::MakeRotateZMatrix(rotateZ);
	Matrix4x4 mTranslate = Matrix4x4::MakeTranslateMatrix({ translate.x, translate.y, 0.0f });
	return Matrix4x4::Multiply(Matrix4x4::Multiply(mScale, mRotZ), mTranslate);
}

// ===================================================================
// リソース生成系ヘルパー関数
// ===================================================================
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, size_t sizeInBytes) {
	D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };
	D3D12_RESOURCE_DESC resourceDesc{
		D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeInBytes, 1, 1, 1,
		DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE
	};
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	return resource;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{
		heapType, numDescriptors,
		shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0
	};
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils,
	const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler,
	const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler)
{
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	DxcBuffer shaderSourceBuffer{ shaderSource->GetBufferPointer(), shaderSource->GetBufferSize(), DXC_CP_UTF8 };

	LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr", L"-HV", L"2021" };
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler.Get(), IID_PPV_ARGS(&shaderResult));

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	return shaderBlob;
}

void LoadTextureFile(const std::wstring& filePath, std::vector<uint8_t>& outData, D3D12_RESOURCE_DESC& outDesc) {
	Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory = nullptr;
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));

	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder = nullptr;
	wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);

	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame = nullptr;
	decoder->GetFrame(0, &frame);

	UINT width = 0, height = 0;
	frame->GetSize(&width, &height);

	outData.resize(width * height * 4);
	frame->CopyPixels(nullptr, width * 4, static_cast<UINT>(outData.size()), outData.data());

	outDesc = {
		D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, width, height, 1, 1,
		DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE
	};
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height) {
	D3D12_RESOURCE_DESC resourceDesc{
		D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)width, (UINT)height, 1, 1,
		DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	};
	D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE depthClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, {1.0f, 0} };

	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	return resource;
}

// 頂点バッファの作成・マップ処理の汎用化
template<typename T>
void CreateVertexBufferView(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const std::vector<T>& vertices,
	Microsoft::WRL::ComPtr<ID3D12Resource>& outResource,
	D3D12_VERTEX_BUFFER_VIEW& outVBV)
{
	size_t sizeInBytes = sizeof(T) * vertices.size();
	outResource = CreateBufferResource(device, sizeInBytes);

	T* vertexData = nullptr;
	outResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, vertices.data(), sizeInBytes);
	outResource->Unmap(0, nullptr);

	outVBV.BufferLocation = outResource->GetGPUVirtualAddress();
	outVBV.SizeInBytes = static_cast<UINT>(sizeInBytes);
	outVBV.StrideInBytes = sizeof(T);
}

// ===================================================================
// モデルロード・生成関数
// ===================================================================
ModelData CreateQuadModel() {
	ModelData modelData;
	modelData.vertices.push_back({ .position = { 1.0f,  1.0f, 0.0f, 1.0f}, .texcoord = {0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f} });
	modelData.vertices.push_back({ .position = {-1.0f,  1.0f, 0.0f, 1.0f}, .texcoord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f} });
	modelData.vertices.push_back({ .position = { 1.0f, -1.0f, 0.0f, 1.0f}, .texcoord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f} });
	modelData.vertices.push_back({ .position = { 1.0f, -1.0f, 0.0f, 1.0f}, .texcoord = {0.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f} });
	modelData.vertices.push_back({ .position = {-1.0f,  1.0f, 0.0f, 1.0f}, .texcoord = {1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f} });
	modelData.vertices.push_back({ .position = {-1.0f, -1.0f, 0.0f, 1.0f}, .texcoord = {1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f} });
	modelData.material.textureFilePath = "./resources/uvChecker.png";
	return modelData;
}

ModelData CreateSphereModel(uint32_t kSubdivision = 32) {
	ModelData modelData;
	const float kPI = 3.1415926535f;

	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat0 = -kPI / 2.0f + kPI * (float)latIndex / kSubdivision;
		float lat1 = -kPI / 2.0f + kPI * (float)(latIndex + 1) / kSubdivision;
		float v0 = 1.0f - (float)latIndex / kSubdivision;
		float v1 = 1.0f - (float)(latIndex + 1) / kSubdivision;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon0 = 2.0f * kPI * (float)lonIndex / kSubdivision;
			float lon1 = 2.0f * kPI * (float)(lonIndex + 1) / kSubdivision;
			float u0 = (float)lonIndex / kSubdivision;
			float u1 = (float)(lonIndex + 1) / kSubdivision;

			auto GetVertex = [](float lat, float lon, float u, float v) {
				Vertex vert;
				vert.position = { std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon), 1.0f };
				vert.normal = { vert.position.x, vert.position.y, vert.position.z };
				vert.texcoord = { u, v };
				return vert;
				};

			Vertex vA = GetVertex(lat0, lon0, u0, v0);
			Vertex vB = GetVertex(lat1, lon0, u0, v1);
			Vertex vC = GetVertex(lat0, lon1, u1, v0);
			Vertex vD = GetVertex(lat1, lon1, u1, v1);

			modelData.vertices.push_back(vA); modelData.vertices.push_back(vB); modelData.vertices.push_back(vC);
			modelData.vertices.push_back(vC); modelData.vertices.push_back(vB); modelData.vertices.push_back(vD);
		}
	}
	return modelData;
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
			Vector4 pos; s >> pos.x >> pos.y >> pos.z; pos.w = 1.0f;
			positions.push_back(pos);
		} else if (identifier == "vt") {
			Vector2 tex; s >> tex.x >> tex.y; tex.y = 1.0f - tex.y;
			texcoords.push_back(tex);
		} else if (identifier == "vn") {
			Vector3 norm; s >> norm.x >> norm.y >> norm.z;
			normals.push_back(norm);
		} else if (identifier == "f") {
			Vertex triangle[3];
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition; s >> vertexDefinition;
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3] = { 0, 0, 0 };

				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					if (std::getline(v, index, '/')) {
						elementIndices[element] = index.empty() ? 0 : std::stoi(index);
					}
				}
				Vector4 pos = (elementIndices[0] > 0) ? positions[elementIndices[0] - 1] : Vector4{ 0, 0, 0, 1 };
				Vector2 tex = (elementIndices[1] > 0) ? texcoords[elementIndices[1] - 1] : Vector2{ 0, 0 };
				Vector3 norm = (elementIndices[2] > 0) ? normals[elementIndices[2] - 1] : Vector3{ 0, 0, -1 };
				pos.x *= -1.0f; norm.x *= -1.0f;
				triangle[faceVertex] = { pos, tex, norm };
			}
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		}
	}
	return modelData;
}

// サウンド機能
SoundData SoundLoadWave(const char* filename) {
	std::ifstream file(filename, std::ios_base::binary);
	assert(file.is_open());

	RiffHeader riff; file.read((char*)&riff, sizeof(riff));
	FormatChunk format = {}; file.read((char*)&format, sizeof(ChunkHeader));
	file.read((char*)&format.fmt, format.chunk.size);

	ChunkHeader data; file.read((char*)&data, sizeof(data));
	if (strncmp(data.id, "JUNK", 4) == 0) {
		file.seekg(data.size, std::ios_base::cur);
		file.read((char*)&data, sizeof(data));
	}
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);
	file.close();

	return { format.fmt, reinterpret_cast<BYTE*>(pBuffer), static_cast<unsigned int>(data.size) };
}

void SoundUnload(SoundData* soundData) {
	delete[] soundData->pBuffer;
	soundData->pBuffer = nullptr;
}

void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData) {
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	if (SUCCEEDED(xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex))) {
		XAUDIO2_BUFFER buf{};
		buf.pAudioData = soundData.pBuffer;
		buf.AudioBytes = soundData.bufferSize;
		buf.Flags = XAUDIO2_END_OF_STREAM;
		pSourceVoice->SubmitSourceBuffer(&buf);
		pSourceVoice->Start();
	}
}

// ===================================================================
// エントリポイント (WinMain)
// ===================================================================
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	D3DResourceLeakChecker leakCheck;

	// ウィンドウ設定
	WNDCLASS wc{ 0, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"CG2WindowClass" };
	RegisterClass(&wc);

	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	HWND hwnd = CreateWindow(
		wc.lpszClassName, L"CG3_Work", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, wrc.right - wrc.left, wrc.bottom - wrc.top,
		nullptr, nullptr, wc.hInstance, nullptr
	);
	ShowWindow(hwnd, SW_SHOW);

	{
		// DXGI & Device
		Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
		CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

		Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
		dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter));

		Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
		D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

		const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		// コマンド関連
		D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
		commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
		device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
		device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
		device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

		// スワップチェーン
		Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = kClientWidth;
		swapChainDesc.Height = kClientHeight;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		dxgiFactory->CreateSwapChainForHwnd(
			commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr,
			reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf())
		);

		// ディスクリプタヒープ
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

		// RTV生成
		Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2];
		swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
		swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D12_RTV_DIMENSION_TEXTURE2D };
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = {
			GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 0),
			GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 1)
		};
		device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
		device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

		// DSV生成
		Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{ DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D };
		device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, GetCPUDescriptorHandle(dsvDescriptorHeap.Get(), descriptorSizeDSV, 0));
		commandList->Close();

		// フェンス
		Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
		uint64_t fenceValue = 0;
		device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

		// 入力・サウンド初期化
		IDirectInput8* directInput = nullptr;
		DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, nullptr);

		IDirectInputDevice8* keyboard = nullptr;
		directInput->CreateDevice(GUID_SysKeyboard, &keyboard, NULL);
		keyboard->SetDataFormat(&c_dfDIKeyboard);
		keyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);

		Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
		IXAudio2MasteringVoice* masterVoice = nullptr;
		XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
		xAudio2->CreateMasteringVoice(&masterVoice);

		SoundData soundData1 = SoundLoadWave("resources/fanfare.wav");
		SoundPlayWave(xAudio2.Get(), soundData1);

		// Shader Compiler 初期化
		Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
		Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
		Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
		DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
		dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

		// ===================================================================
		// ★ Root Signature の構成（資料12の手順に基づく構成）
		// ===================================================================
		D3D12_ROOT_PARAMETER rootParameters[3] = {};

		// [0] Material (CBV, b0, Pixel)
		rootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL };
		rootParameters[0].Descriptor.ShaderRegister = 0;

		// [1] StructuredBuffer Instancing Data (Descriptor Table, t0, Vertex)
		D3D12_DESCRIPTOR_RANGE descriptorRangeInstancing[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
		rootParameters[1] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_VERTEX };
		rootParameters[1].DescriptorTable = { 1, descriptorRangeInstancing };

		// [2] Texture (Descriptor Table, t0, Pixel)
		D3D12_DESCRIPTOR_RANGE descriptorRangeTexture[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
		rootParameters[2] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL };
		rootParameters[2].DescriptorTable = { 1, descriptorRangeTexture };

		D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {
			{D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0, 0, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL}
		};

		D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{
			_countof(rootParameters), rootParameters, _countof(staticSamplers), staticSamplers,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		};
		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
		D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
		device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

		// インプットレイアウト
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Particle.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
		Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Particle.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);

		// パイプラインステート生成
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSignature.Get();
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
		psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
		psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
		psoDesc.DepthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL };
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates[kCountOfBlendMode];
		for (int i = 0; i < kCountOfBlendMode; ++i) {
			psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
			psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

			switch (i) {
			case kBlendModeNone:
				psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
				break;
			case kBlendModeNormal:
				psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				break;
			case kBlendModeAdd:
				psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
				break;
			case kBlendModeSubtract:
				psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
				psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
				break;
			case kBlendModeMultiply:
				psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
				psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
				break;
			case kBlendModeScreen:
				psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
				psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
				break;
			}

			HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStates[i]));
			assert(SUCCEEDED(hr));
		}

		// ===================================================================
		// メッシュ・バッファデータ生成
		// ===================================================================
		ModelData planeModelData = LoadObjFile("resources", "plane.obj");
		ModelData sphereModelData = CreateSphereModel(32);
		ModelData fenceModelData = LoadObjFile("resources", "fence.obj");
		ModelData quadModelData = CreateQuadModel();

		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourcePlane, vertexResourceSphere, vertexResourceFence, vertexResourceQuad;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewPlane{}, vertexBufferViewSphere{}, vertexBufferViewFence{}, vertexBufferViewQuad{};

		CreateVertexBufferView(device, planeModelData.vertices, vertexResourcePlane, vertexBufferViewPlane);
		CreateVertexBufferView(device, sphereModelData.vertices, vertexResourceSphere, vertexBufferViewSphere);
		CreateVertexBufferView(device, fenceModelData.vertices, vertexResourceFence, vertexBufferViewFence);
		CreateVertexBufferView(device, quadModelData.vertices, vertexResourceQuad, vertexBufferViewQuad);

		// スプライト用バッファ
		std::vector<Vertex> spriteVertices = {
			{ {0.0f, 360.0f, 0.0f, 1.0f},   {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f} },
			{ {0.0f, 0.0f, 0.0f, 1.0f},     {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f} },
			{ {640.0f, 360.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f} },
			{ {640.0f, 0.0f, 0.0f, 1.0f},   {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f} }
		};
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
		CreateVertexBufferView(device, spriteVertices, vertexResourceSprite, vertexBufferViewSprite);

		std::vector<uint32_t> spriteIndices = { 0, 1, 2, 1, 3, 2 };
		Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * spriteIndices.size());
		D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{ indexResourceSprite->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(uint32_t) * spriteIndices.size()), DXGI_FORMAT_R32_UINT };
		uint32_t* indexDataSprite = nullptr;
		indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
		std::memcpy(indexDataSprite, spriteIndices.data(), sizeof(uint32_t) * spriteIndices.size());
		indexResourceSprite->Unmap(0, nullptr);

		// マテリアルバッファ
		size_t materialSize = (sizeof(Material) + 255) & ~255;
		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = CreateBufferResource(device, materialSize);
		Material* materialData = nullptr;
		materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
		materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		materialData->enableLighting = 0;
		materialData->lightingType = 0;
		materialData->enableAlphaDiscard = 0;
		materialData->discardThreshold = 0.5f;
		materialData->uvTransform = Matrix4x4::MakeIdentity4x4();

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device, materialSize);
		Material* materialDataSprite = nullptr;
		materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
		materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		materialDataSprite->enableLighting = 0;
		materialDataSprite->lightingType = 0;
		materialDataSprite->enableAlphaDiscard = 0;
		materialDataSprite->discardThreshold = 0.0f;
		materialDataSprite->uvTransform = Matrix4x4::MakeIdentity4x4();

		// インスタンシング描画用 StructuredBuffer (資料14)
		const uint32_t kMaxInstanceCount = 100;
		Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource = CreateBufferResource(device, sizeof(ParticleForGPU) * kMaxInstanceCount);
		ParticleForGPU* instancingData = nullptr;
		instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

		Transform transformModel;
		Transform transformSprite;
		Transform uvTransformSprite;

		// テクスチャロード
		std::vector<uint8_t> textureData1, textureData2, textureData3;
		D3D12_RESOURCE_DESC textureDesc1{}, textureDesc2{}, textureDesc3{};
		LoadTextureFile(L"Resources/uvChecker.png", textureData1, textureDesc1);
		LoadTextureFile(L"Resources/monsterBall.png", textureData2, textureDesc2);
		LoadTextureFile(L"Resources/fence.png", textureData3, textureDesc3);

		D3D12_HEAP_PROPERTIES textureHeapProps{ D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0 };
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource1, textureResource2, textureResource3;

		device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc1, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource1));
		textureResource1->WriteToSubresource(0, nullptr, textureData1.data(), (UINT)(textureDesc1.Width * 4), (UINT)textureData1.size());

		device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc2, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource2));
		textureResource2->WriteToSubresource(0, nullptr, textureData2.data(), (UINT)(textureDesc2.Width * 4), (UINT)textureData2.size());

		device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc3, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource3));
		textureResource3->WriteToSubresource(0, nullptr, textureData3.data(), (UINT)(textureDesc3.Width * 4), (UINT)textureData3.size());

		// SRV作成
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{ textureDesc1.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc1.Texture2D.MipLevels = 1;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{ textureDesc2.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc2.Texture2D.MipLevels = 1;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc3{ textureDesc3.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc3.Texture2D.MipLevels = 1;

		device->CreateShaderResourceView(textureResource1.Get(), &srvDesc1, GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1));
		device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2));
		device->CreateShaderResourceView(textureResource3.Get(), &srvDesc3, GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3));

		// StructuredBuffer 用 SRV の作成（資料15）
		D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);
		D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
		instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		instancingSrvDesc.Buffer.FirstElement = 0;
		instancingSrvDesc.Buffer.NumElements = kMaxInstanceCount;
		instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
		instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4));

		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 1.0f };
		D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

#ifdef USE_IMGUI
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX12_Init(
			device.Get(), 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvDescriptorHeap.Get(),
			GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0),
			GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0)
		);
#endif

		DebugCamera debugCamera;
		debugCamera.Initialize();

		bool isAutoRotate = false;
		float pi_f = 3.1415926535f;
		bool drawSprite = true;

		int currentModelType = 3;   // デフォルト: Particle Quad
		int currentTextureType = 0; // デフォルト: uvChecker
		int instanceCount = 10;     // インスタンス数
		int currentBlendMode = kBlendModeNone;

		// ===================================================================
		// メインループ
		// ===================================================================
		MSG msg{};
		while (msg.message != WM_QUIT) {
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			} else {
				std::memcpy(keyPre, key, sizeof(key));
				keyboard->Acquire();
				keyboard->GetDeviceState(sizeof(key), key);
				debugCamera.Update(key);

				if (isAutoRotate) {
					transformModel.rotate.y += 0.01f;
					if (transformModel.rotate.y > pi_f * 2.0f) transformModel.rotate.y -= pi_f * 2.0f;
				}

#ifdef USE_IMGUI
				ImGui_ImplDX12_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();
				ImGui::Begin("Settings");

				const char* blendItems[] = { "kBlendModeNone", "kBlendModeNormal", "kBlendModeAdd", "kBlendModeSubtract", "kBlendModeMultiply", "kBlendModeScreen" };
				ImGui::Combo("Blend Mode", &currentBlendMode, blendItems, IM_ARRAYSIZE(blendItems));

				const char* modelNames[] = { "Plane", "Sphere", "Fence", "Particle Quad" };
				if (ImGui::Combo("Model Select", &currentModelType, modelNames, IM_ARRAYSIZE(modelNames))) {
					if (currentModelType == 2) currentTextureType = 2; // Fence
					else currentTextureType = 0;                       // Plane / Sphere / Quad
				}

				if (ImGui::CollapsingHeader("Instancing Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::SliderInt("Instance Count", &instanceCount, 1, static_cast<int>(kMaxInstanceCount));
				}

				const char* textureNames[] = { "uvChecker", "monsterBall", "fence" };
				ImGui::Combo("Texture Select", &currentTextureType, textureNames, IM_ARRAYSIZE(textureNames));

				if (ImGui::CollapsingHeader("Object (SRT)", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat3("Translate", &transformModel.translate.x, 0.01f);
					ImGui::SliderFloat3("Rotate", &transformModel.rotate.x, -pi_f, pi_f);
					ImGui::DragFloat3("Scale", &transformModel.scale.x, 0.01f);
					ImGui::Checkbox("Auto Rotate", &isAutoRotate);
				}

				if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::ColorEdit4("Color", &materialData->color.x);
				}

				if (ImGui::CollapsingHeader("Sprite Options", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Draw Sprite", &drawSprite);
					if (drawSprite) {
						ImGui::SliderFloat2("Sprite Scale", &transformSprite.scale.x, 0.1f, 5.0f);
						ImGui::SliderFloat2("Sprite Translate", &transformSprite.translate.x, -200.0f, 1000.0f);
						ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
						ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
						ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
					}
				}

				ImGui::End();
				ImGui::Render();
#endif

				// 行列・データ更新
				Matrix4x4 materialTransformMatrix = MakeUVTransformMatrix(uvTransformSprite.scale, uvTransformSprite.rotate.z, uvTransformSprite.translate);
				materialDataSprite->uvTransform = materialTransformMatrix;

				Matrix4x4 viewMatrix = debugCamera.GetMatView();
				Matrix4x4 projectionMatrixModel = Matrix4x4::MakePerspectiveFovMatrix(0.45f, (float)kClientWidth / kClientHeight, 0.1f, 100.0f);

				// インスタンスデータの書き込み (資料16)
				for (int i = 0; i < instanceCount; ++i) {
					Transform instanceTransform = transformModel;

					// 格子状のオフセット加算 (5列配置)
					float offsetX = (i % 5 - 2.0f) * 2.5f;
					float offsetY = (i / 5 - 2.0f) * 2.5f;
					instanceTransform.translate.x += offsetX;
					instanceTransform.translate.y += offsetY;

					Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(instanceTransform.scale, instanceTransform.rotate, instanceTransform.translate);
					Matrix4x4 wvpMatrix = Matrix4x4::Multiply(Matrix4x4::Multiply(worldMatrix, viewMatrix), projectionMatrixModel);

					instancingData[i].wvp = wvpMatrix;
					instancingData[i].world = worldMatrix;
					instancingData[i].color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
				}

				// 描画開始処理
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

				// 安全チェック (資料17)
				assert(instanceCount <= static_cast<int>(kMaxInstanceCount));

				commandList->RSSetViewports(1, &viewport);
				commandList->RSSetScissorRects(1, &scissorRect);
				commandList->SetGraphicsRootSignature(rootSignature.Get());
				commandList->SetPipelineState(pipelineStates[currentBlendMode].Get());
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

				// RootParameters へのバインド設定（資料12・17）
				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // RootParam[0]: Material CBV (b0)
				commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);                      // RootParam[1]: Instancing SRV (t0)

				D3D12_GPU_DESCRIPTOR_HANDLE selectedSrvGPU = textureSrvHandleGPU1;
				if (currentTextureType == 1) selectedSrvGPU = textureSrvHandleGPU2;
				else if (currentTextureType == 2) selectedSrvGPU = textureSrvHandleGPU3;
				commandList->SetGraphicsRootDescriptorTable(2, selectedSrvGPU);                             // RootParam[2]: Texture SRV (t0)

				// 1. 3Dモデル描画 (DrawInstanced 一括呼び出し)
				if (currentModelType == 0) {
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewPlane);
					commandList->DrawInstanced(static_cast<UINT>(planeModelData.vertices.size()), instanceCount, 0, 0);
				} else if (currentModelType == 1) {
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);
					commandList->DrawInstanced(static_cast<UINT>(sphereModelData.vertices.size()), instanceCount, 0, 0);
				} else if (currentModelType == 2) {
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewFence);
					commandList->DrawInstanced(static_cast<UINT>(fenceModelData.vertices.size()), instanceCount, 0, 0);
				} else if (currentModelType == 3) {
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewQuad);
					commandList->DrawInstanced(static_cast<UINT>(quadModelData.vertices.size()), instanceCount, 0, 0);
				}

				// 2. 2Dスプライト描画
				if (drawSprite) {
					Matrix4x4 worldMatrixSprite = Matrix4x4::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
					Matrix4x4 projectionMatrixSprite = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 100.0f);
					instancingData[0].wvp = Matrix4x4::Multiply(worldMatrixSprite, projectionMatrixSprite);
					instancingData[0].world = worldMatrixSprite;
					instancingData[0].color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };

					commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
					commandList->IASetIndexBuffer(&indexBufferViewSprite);
					commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
				}

#ifdef USE_IMGUI
				ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

				// 画面表示コマンド発行
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

		if (keyboard) { keyboard->Unacquire(); keyboard->Release(); }
		if (directInput) directInput->Release();
		xAudio2.Reset();
		SoundUnload(&soundData1);

		instancingResource->Unmap(0, nullptr);
		materialResourceSprite->Unmap(0, nullptr);
		materialResource->Unmap(0, nullptr);
		CloseHandle(fenceEvent);
	}

	UnregisterClass(wc.lpszClassName, wc.hInstance);
	CoUninitialize();
	return 0;
}