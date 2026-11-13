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

#define USE_IMGUI
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

// キーバッファ
BYTE key[256] = {};
BYTE keyPre[256] = {};

bool IsKeyPressed(uint8_t keyCode) { return (key[keyCode] & 0x80) != 0; }
bool IsKeyReleased(uint8_t keyCode) { return (key[keyCode] & 0x80) == 0; }
bool IsKeyTriggered(uint8_t keyCode) { return ((key[keyCode] & 0x80) != 0) && ((keyPre[keyCode] & 0x80) == 0); }

struct ChunkHeader { char id[4]; int32_t size; };
struct RiffHeader { ChunkHeader chunk; char type[4]; };
struct FormatChunk { ChunkHeader chunk; WAVEFORMATEX fmt; };
struct SoundData { WAVEFORMATEX wfex; BYTE* pBuffer; unsigned int bufferSize; };

SoundData SoundLoadWave(const char* filename)
{
	std::ifstream file(filename, std::ios_base::binary);
	assert(file.is_open());
	RiffHeader riff; file.read((char*)&riff, sizeof(riff));
	FormatChunk format = {}; file.read((char*)&format, sizeof(ChunkHeader));
	file.read((char*)&format.fmt, format.chunk.size);
	ChunkHeader data; file.read((char*)&data, sizeof(data));
	if (strncmp(data.id, "JUNK", 4) == 0)
	{
		file.seekg(data.size, std::ios_base::cur);
		file.read((char*)&data, sizeof(data));
	}
	char* pBuffer = new char[data.size]; file.read(pBuffer, data.size);
	file.close();
	return { format.fmt, reinterpret_cast<BYTE*>(pBuffer), static_cast<unsigned int>(data.size) };
}

void SoundUnload(SoundData* soundData) { delete[] soundData->pBuffer; soundData->pBuffer = nullptr; }
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData)
{
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	if (SUCCEEDED(xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex)))
	{
		XAUDIO2_BUFFER buf{}; buf.pAudioData = soundData.pBuffer; buf.AudioBytes = soundData.bufferSize; buf.Flags = XAUDIO2_END_OF_STREAM;
		pSourceVoice->SubmitSourceBuffer(&buf); pSourceVoice->Start();
	}
}

struct D3DResourceLeakChecker
{
	~D3DResourceLeakChecker()
	{
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
		{
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	}
};

std::wstring ConvertString(const std::string& str)
{
	if (str.empty()) return L"";
	int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
	if (sizeNeeded == 0) sizeNeeded = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
	std::wstring wstr(sizeNeeded - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], sizeNeeded);
	return wstr;
}

// 補助用構造体
struct Vector2 { float x, y; };
struct Vector4 { float x, y, z, w; };
struct Vertex { Vector4 position; Vector2 texcoord; Vector3 normal; };

struct MaterialData { std::string textureFilePath; };
struct ModelData { std::vector<Vertex> vertices; MaterialData material; };

struct Material
{
	Vector4 color;
	int32_t enableLighting;     // 0: Disable, 1: Enable
	int32_t lightingType;       // 0: None, 1: Lambert, 2: Half Lambert
	int32_t enableAlphaDiscard; // 0: 無効, 1: 有効
	float discardThreshold;     // 棄却アルファ閾値
	Matrix4x4 uvTransform;
};

struct TransformationMatrix { Matrix4x4 wvp; Matrix4x4 world; };

// GPUのStructuredBufferへ転送するパーティクル1粒分の構造体
struct ParticleForGPU
{
	Matrix4x4 wvp;
	Matrix4x4 world;
	Vector4 color;
};

struct Transform
{
	Vector3 scale{ 1.0f, 1.0f, 1.0f };
	Vector3 rotate{ 0.0f, 0.0f, 0.0f };
	Vector3 translate{ 0.0f, 0.0f, 0.0f };
};

struct Particle
{
	Transform transform;
	Vector3 velocity;
	Vector4 color;
};

struct DirectionalLight { Vector4 color; Vector3 direction; float intensity; };

float Clamp01(float v)
{
	if (v < 0.0f) return 0.0f;
	if (v > 1.0f) return 1.0f;
	return v;
}

Vector4 LerpColor(const Vector4& a, const Vector4& b, float t)
{
	float s = Clamp01(t);
	return {
		a.x + (b.x - a.x) * s,
		a.y + (b.y - a.y) * s,
		a.z + (b.z - a.z) * s,
		a.w + (b.w - a.w) * s
	};
}

Vector3 Vec3Cross(const Vector3& a, const Vector3& b)
{
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

float Vec3Length(const Vector3& v)
{
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Matrix4x4 MakeUVTransformMatrix(const Vector3& scale, float rotateZ, const Vector3& translate)
{
	Matrix4x4 mScale = Matrix4x4::MakeScaleMatrix({ scale.x, scale.y, 1.0f });
	Matrix4x4 mRotZ = Matrix4x4::MakeRotateZMatrix(rotateZ);
	Matrix4x4 mTranslate = Matrix4x4::MakeTranslateMatrix({ translate.x, translate.y, 0.0f });
	return Matrix4x4::Multiply(Matrix4x4::Multiply(mScale, mRotZ), mTranslate);
}

ModelData CreateSphereModel(uint32_t kSubdivision = 32)
{
	ModelData modelData;
	const float kPI = 3.1415926535f;
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex)
	{
		float lat0 = -kPI / 2.0f + kPI * (float)latIndex / kSubdivision;
		float lat1 = -kPI / 2.0f + kPI * (float)(latIndex + 1) / kSubdivision;
		float v0 = 1.0f - (float)latIndex / kSubdivision;
		float v1 = 1.0f - (float)(latIndex + 1) / kSubdivision;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex)
		{
			float lon0 = 2.0f * kPI * (float)lonIndex / kSubdivision;
			float lon1 = 2.0f * kPI * (float)(lonIndex + 1) / kSubdivision;
			float u0 = (float)lonIndex / kSubdivision;
			float u1 = (float)(lonIndex + 1) / kSubdivision;

			auto GetVertex = [](float lat, float lon, float u, float v)
				{
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

ModelData CreateParticleModel()
{
	ModelData modelData;
	modelData.vertices.push_back({ .position = { 1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左上
	modelData.vertices.push_back({ .position = {-1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右上
	modelData.vertices.push_back({ .position = { 1.0f,-1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左下
	modelData.vertices.push_back({ .position = { 1.0f,-1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左下
	modelData.vertices.push_back({ .position = {-1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右上
	modelData.vertices.push_back({ .position = {-1.0f,-1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右下
	modelData.material.textureFilePath = "./resources/circle.png";
	return modelData;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
#endif
	if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile, const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils, const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler, const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler)
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

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{ heapType, numDescriptors, shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
	device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	return descriptorHeap;
}

void LoadTextureFile(const std::wstring& filePath, std::vector<uint8_t>& outData, D3D12_RESOURCE_DESC& outDesc)
{
	Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory = nullptr;
	CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
	Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder = nullptr;
	wicFactory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame = nullptr; decoder->GetFrame(0, &frame);
	UINT width = 0, height = 0; frame->GetSize(&width, &height);
	outData.resize(width * height * 4);
	frame->CopyPixels(nullptr, width * 4, static_cast<UINT>(outData.size()), outData.data());
	outDesc = { D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE };
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height)
{
	D3D12_RESOURCE_DESC resourceDesc{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)width, (UINT)height, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL };
	D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE depthClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, {1.0f, 0} };
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	return resource;
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename)
{
	ModelData modelData;
	std::vector<Vector4> positions; std::vector<Vector3> normals; std::vector<Vector2> texcoords;
	std::string line; std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());
	while (std::getline(file, line))
	{
		std::string identifier; std::istringstream s(line); s >> identifier;
		if (identifier == "v") { Vector4 pos; s >> pos.x >> pos.y >> pos.z; pos.w = 1.0f; positions.push_back(pos); } else if (identifier == "vt") { Vector2 tex; s >> tex.x >> tex.y; tex.y = 1.0f - tex.y; texcoords.push_back(tex); } else if (identifier == "vn") { Vector3 norm; s >> norm.x >> norm.y >> norm.z; normals.push_back(norm); } else if (identifier == "f")
		{
			Vertex triangle[3];
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
			{
				std::string vertexDefinition; s >> vertexDefinition; std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3] = { 0,0,0 };
				for (int32_t element = 0; element < 3; ++element)
				{
					std::string index; if (std::getline(v, index, '/')) elementIndices[element] = index.empty() ? 0 : std::stoi(index);
				}
				Vector4 pos = (elementIndices[0] > 0) ? positions[elementIndices[0] - 1] : Vector4{ 0,0,0,1 };
				Vector2 tex = (elementIndices[1] > 0) ? texcoords[elementIndices[1] - 1] : Vector2{ 0,0 };
				Vector3 norm = (elementIndices[2] > 0) ? normals[elementIndices[2] - 1] : Vector3{ 0,0,-1 };
				pos.x *= -1.0f; norm.x *= -1.0f;
				triangle[faceVertex] = { pos, tex, norm };
			}
			modelData.vertices.push_back(triangle[2]); modelData.vertices.push_back(triangle[1]); modelData.vertices.push_back(triangle[0]);
		}
	}
	return modelData;
}

// 画面切り替え用
enum class ScreenMode {
	Model3D = 0,   // 3Dモデル確認画面 (Plane / Sphere / Fence)
	Particle = 1,  // パーティクル(Instancing)確認画面
	Sprite2D = 2,  // 2Dスプライト確認画面
	Tornado = 3,   // 竜巻演出画面
	Snow = 4,      // 雪演出画面
	Meteor = 5,    // 隕石からの爆発演出画面
	Magic = 6,     // 魔法陣からの魔法発射演出画面
	All = 7        // すべて同時表示
};

ScreenMode currentScreen = ScreenMode::Model3D;
int currentModelType = 0; // 0: Plane, 1: Sphere, 2: Fence

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	D3DResourceLeakChecker leakCheck;

	WNDCLASS wc{ 0, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"CG2WindowClass" };
	RegisterClass(&wc);
	const int32_t kClientWidth = 1280; const int32_t kClientHeight = 720;
	RECT wrc = { 0, 0, kClientWidth, kClientHeight }; AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);
	HWND hwnd = CreateWindow(wc.lpszClassName, L"LE2C_09_カワムラ_リョウマ", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wrc.right - wrc.left, wrc.bottom - wrc.top, nullptr, nullptr, wc.hInstance, nullptr);
	ShowWindow(hwnd, SW_SHOW);

	{
		Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr; CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
		Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
		dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter));
		Microsoft::WRL::ComPtr<ID3D12Device> device = nullptr;
		D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

		const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		D3D12_COMMAND_QUEUE_DESC commandQueueDesc{}; commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr; device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr; device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr; device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

		Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		swapChainDesc.Width = kClientWidth; swapChainDesc.Height = kClientHeight; swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.SampleDesc.Count = 1; swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; swapChainDesc.BufferCount = 2;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH; swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

		Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2]; swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0])); swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D12_RTV_DIMENSION_TEXTURE2D };
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = { GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 0), GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, 1) };
		device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
		device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

		Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{ DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D };
		device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, GetCPUDescriptorHandle(dsvDescriptorHeap.Get(), descriptorSizeDSV, 0));
		commandList->Close();

		Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr; uint64_t fenceValue = 0; device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

		// DirectInput
		IDirectInput8* directInput = nullptr; DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, nullptr);
		IDirectInputDevice8* keyboard = nullptr; directInput->CreateDevice(GUID_SysKeyboard, &keyboard, NULL);
		keyboard->SetDataFormat(&c_dfDIKeyboard); keyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);

		// XAudio2
		Microsoft::WRL::ComPtr<IXAudio2> xAudio2; IXAudio2MasteringVoice* masterVoice = nullptr;
		XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR); xAudio2->CreateMasteringVoice(&masterVoice);
		SoundData soundData1 = SoundLoadWave("resources/fanfare.wav"); SoundPlayWave(xAudio2.Get(), soundData1);

		Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils; Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler; Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
		DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils)); DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler)); dxcUtils->CreateDefaultIncludeHandler(&includeHandler);

		// ===== Object3d用 RootSignature の作成 =====
		D3D12_ROOT_PARAMETER rootParameters[4] = {};
		rootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[0].Descriptor.ShaderRegister = 0;
		rootParameters[1] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_VERTEX }; rootParameters[1].Descriptor.ShaderRegister = 1;
		D3D12_DESCRIPTOR_RANGE descriptorRange[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
		rootParameters[2] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[2].DescriptorTable = { 1, descriptorRange };
		rootParameters[3] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[3].Descriptor.ShaderRegister = 1;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = { {D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 0, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL} };

		D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{ _countof(rootParameters), rootParameters, _countof(staticSamplers), staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr; Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr; D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr; device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

		// ===== Particle用 RootSignature の作成 =====
		D3D12_ROOT_PARAMETER particleRootParameters[3] = {};
		particleRootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL };
		particleRootParameters[0].Descriptor.ShaderRegister = 0; // b0: Material

		D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
		descriptorRangeForInstancing[0].BaseShaderRegister = 0; // t0から始まる
		descriptorRangeForInstancing[0].NumDescriptors = 1;     // 数は1つ
		descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
		descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		particleRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
		particleRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
		particleRootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing; // Tableの中身の配列を指定
		particleRootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する数

		// テクスチャ用 SRV 
		D3D12_DESCRIPTOR_RANGE descriptorRangeForTexture[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
		particleRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		particleRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		particleRootParameters[2].DescriptorTable = { 1, descriptorRangeForTexture };

		D3D12_ROOT_SIGNATURE_DESC particleRootSignatureDesc{ _countof(particleRootParameters), particleRootParameters, _countof(staticSamplers), staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
		Microsoft::WRL::ComPtr<ID3DBlob> particleSignatureBlob = nullptr;
		D3D12SerializeRootSignature(&particleRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &particleSignatureBlob, &errorBlob);
		Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature = nullptr;
		device->CreateRootSignature(0, particleSignatureBlob->GetBufferPointer(), particleSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&particleRootSignature));

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		// Object3d用 PSO
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSignature.Get(); psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
		Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
		psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };

		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
		psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

		psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
		psoDesc.DepthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL }; psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; psoDesc.SampleDesc.Count = 1; psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState = nullptr; device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));

		// Particle用 PSO
		Microsoft::WRL::ComPtr<IDxcBlob> particleVSBlob = CompileShader(L"Particle.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
		Microsoft::WRL::ComPtr<IDxcBlob> particlePSBlob = CompileShader(L"Particle.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePsoDesc = psoDesc;
		particlePsoDesc.pRootSignature = particleRootSignature.Get();
		particlePsoDesc.VS = { particleVSBlob->GetBufferPointer(), particleVSBlob->GetBufferSize() };
		particlePsoDesc.PS = { particlePSBlob->GetBufferPointer(), particlePSBlob->GetBufferSize() };
		particlePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> particlePipelineState = nullptr;
		device->CreateGraphicsPipelineState(&particlePsoDesc, IID_PPV_ARGS(&particlePipelineState));

		D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };

		ModelData planeModelData = LoadObjFile("resources", "plane.obj");
		ModelData sphereModelData = CreateSphereModel(32);
		ModelData fenceModelData = LoadObjFile("resources", "fence.obj");
		ModelData particleModelData = CreateParticleModel();

		// Planeバッファ
		D3D12_RESOURCE_DESC vertexResourceDescPlane{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * planeModelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourcePlane = nullptr;
		device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescPlane, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourcePlane));
		Vertex* vertexDataPlane = nullptr; vertexResourcePlane->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataPlane));
		std::memcpy(vertexDataPlane, planeModelData.vertices.data(), sizeof(Vertex) * planeModelData.vertices.size());
		vertexResourcePlane->Unmap(0, nullptr);
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewPlane{ vertexResourcePlane->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(Vertex) * planeModelData.vertices.size()), sizeof(Vertex) };

		// Sphereバッファ
		D3D12_RESOURCE_DESC vertexResourceDescSphere{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * sphereModelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere = nullptr;
		device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescSphere, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSphere));
		Vertex* vertexDataSphere = nullptr; vertexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSphere));
		std::memcpy(vertexDataSphere, sphereModelData.vertices.data(), sizeof(Vertex) * sphereModelData.vertices.size());
		vertexResourceSphere->Unmap(0, nullptr);
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere{ vertexResourceSphere->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(Vertex) * sphereModelData.vertices.size()), sizeof(Vertex) };

		// フェンス用の頂点バッファを作成
		D3D12_RESOURCE_DESC vertexResourceDescFence{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * fenceModelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceFence = nullptr;
		device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescFence, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceFence));
		Vertex* vertexDataFence = nullptr;
		vertexResourceFence->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataFence));
		std::memcpy(vertexDataFence, fenceModelData.vertices.data(), sizeof(Vertex) * fenceModelData.vertices.size());
		vertexResourceFence->Unmap(0, nullptr);
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewFence{ vertexResourceFence->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(Vertex) * fenceModelData.vertices.size()), sizeof(Vertex) };

		// パーティクル用の頂点バッファを作成
		D3D12_RESOURCE_DESC vertexResourceDescParticle{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * particleModelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceParticle = nullptr;
		device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescParticle, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceParticle));
		Vertex* vertexDataParticle = nullptr;
		vertexResourceParticle->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataParticle));
		std::memcpy(vertexDataParticle, particleModelData.vertices.data(), sizeof(Vertex) * particleModelData.vertices.size());
		vertexResourceParticle->Unmap(0, nullptr);
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewParticle{ vertexResourceParticle->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(Vertex) * particleModelData.vertices.size()), sizeof(Vertex) };

		// Sprite Vertex Buffer
		D3D12_RESOURCE_DESC vertexResourceDescSprite{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * 4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSprite));
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{ vertexResourceSprite->GetGPUVirtualAddress(), sizeof(Vertex) * 4, sizeof(Vertex) };
		Vertex* vertexDataSprite = nullptr; vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
		Vector3 nSprite = { 0.0f, 0.0f, -1.0f };
		vertexDataSprite[0] = { {0.0f, 360.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, nSprite };
		vertexDataSprite[1] = { {0.0f, 0.0f, 0.0f, 1.0f},   {0.0f, 0.0f}, nSprite };
		vertexDataSprite[2] = { {640.0f, 360.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, nSprite };
		vertexDataSprite[3] = { {640.0f, 0.0f, 0.0f, 1.0f},   {1.0f, 0.0f}, nSprite };
		vertexResourceSprite->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC indexResourceDescSprite{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(uint32_t) * 6, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &indexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexResourceSprite));
		D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{ indexResourceSprite->GetGPUVirtualAddress(), sizeof(uint32_t) * 6, DXGI_FORMAT_R32_UINT };
		uint32_t* indexDataSprite = nullptr; indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
		indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2; indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;
		indexResourceSprite->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC materialResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(Material) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResource));
		Material* materialData = nullptr; materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
		materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; materialData->enableLighting = 1; materialData->lightingType = 2; // デフォルト: Half Lambert
		materialData->uvTransform = Matrix4x4::MakeIdentity4x4();

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResourceSprite));
		Material* materialDataSprite = nullptr; materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
		materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f }; materialDataSprite->enableLighting = 0; materialDataSprite->lightingType = 0;
		materialDataSprite->uvTransform = Matrix4x4::MakeIdentity4x4();

		D3D12_RESOURCE_DESC wvpResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(TransformationMatrix) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&wvpResource));
		TransformationMatrix* wvpData = nullptr; wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
		wvpData->wvp = Matrix4x4::MakeIdentity4x4(); wvpData->world = Matrix4x4::MakeIdentity4x4();

		Transform transformModel;

		Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformationMatrixResourceSprite));
		TransformationMatrix* transformationMatrixDataSprite = nullptr; transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
		transformationMatrixDataSprite->wvp = Matrix4x4::MakeIdentity4x4(); transformationMatrixDataSprite->world = Matrix4x4::MakeIdentity4x4();
		Transform transformSprite;
		Transform uvTransformSprite;

		// インスタンシング用バッファ
		const uint32_t kNumInstance = 600; // インスタンス数
		D3D12_RESOURCE_DESC instancingResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(ParticleForGPU) * kNumInstance, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource = nullptr;
		device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &instancingResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instancingResource));

		// 書き込むためのアドレスを取得
		ParticleForGPU* instancingData = nullptr;
		instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));
		// 単位行列と白色を書きこんでおく
		for (uint32_t index = 0; index < kNumInstance; ++index)
		{
			instancingData[index].wvp = Matrix4x4::MakeIdentity4x4();
			instancingData[index].world = Matrix4x4::MakeIdentity4x4();
			instancingData[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };
		}

		D3D12_RESOURCE_DESC directionalLightResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(DirectionalLight) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = nullptr; device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &directionalLightResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&directionalLightResource));
		DirectionalLight* directionalLightData = nullptr; directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
		directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; directionalLightData->direction = { 0.0f, -1.0f, 0.0f }; directionalLightData->intensity = 1.0f;

		// Textures
		D3D12_HEAP_PROPERTIES textureHeapProps{ D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0 };

		std::vector<uint8_t> textureData; D3D12_RESOURCE_DESC textureDesc{}; LoadTextureFile(L"Resources/uvChecker.png", textureData, textureDesc);
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource));
		textureResource->WriteToSubresource(0, nullptr, textureData.data(), (UINT)(textureDesc.Width * 4), (UINT)textureData.size());

		std::vector<uint8_t> textureData2; D3D12_RESOURCE_DESC textureDesc2{}; LoadTextureFile(L"Resources/monsterBall.png", textureData2, textureDesc2);
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2 = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc2, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource2));
		textureResource2->WriteToSubresource(0, nullptr, textureData2.data(), (UINT)(textureDesc2.Width * 4), (UINT)textureData2.size());

		std::vector<uint8_t> textureDataFence; D3D12_RESOURCE_DESC textureDescFence{}; LoadTextureFile(L"Resources/fence.png", textureDataFence, textureDescFence);
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResourceFence = nullptr;	device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDescFence, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResourceFence));
		textureResourceFence->WriteToSubresource(0, nullptr, textureDataFence.data(), (UINT)(textureDescFence.Width * 4), (UINT)textureDataFence.size());

		// circle.png の読み込み
		std::vector<uint8_t> textureDataCircle; D3D12_RESOURCE_DESC textureDescCircle{}; LoadTextureFile(L"Resources/circle.png", textureDataCircle, textureDescCircle);
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResourceCircle = nullptr; device->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDescCircle, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResourceCircle));
		textureResourceCircle->WriteToSubresource(0, nullptr, textureDataCircle.data(), (UINT)(textureDescCircle.Width * 4), (UINT)textureDataCircle.size());

		// SRV 1: uvChecker.png
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU1 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{ textureDesc.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc1.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(textureResource.Get(), &srvDesc1, textureSrvHandleCPU1);

		// SRV 2: monsterBall.png
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{ textureDesc2.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc2.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

		// SRV 3: fence.png
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 3);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescFence{ textureDescFence.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING };	srvDescFence.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(textureResourceFence.Get(), &srvDescFence, textureSrvHandleCPU3);

		// SRV 4: Instancing用 StructuredBuffer
		D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
		instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
		instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		instancingSrvDesc.Buffer.FirstElement = 0;
		instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		instancingSrvDesc.Buffer.NumElements = kNumInstance;
		instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

		D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);
		D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 4);
		device->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

		// SRV 5: circle.png
		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU5 = GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 5);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU5 = GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 5);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescCircle{ textureDescCircle.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDescCircle.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(textureResourceCircle.Get(), &srvDescCircle, textureSrvHandleCPU5);

		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 1.0f };
		D3D12_RECT scissorRect{ 0, 0, kClientWidth, kClientHeight };

#ifdef USE_IMGUI
		IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::StyleColorsDark(); ImGui_ImplWin32_Init(hwnd);
		ImGui_ImplDX12_Init(device.Get(), 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srvDescriptorHeap.Get(), GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0), GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 0));
#endif

		DebugCamera debugCamera; debugCamera.Initialize();
		bool isAutoRotate = false; bool useMonsterBall = false;
		float pi_f = 3.1415926535f;

		Particle particles[kNumInstance];
		Vector4 particleBaseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		bool isParticleMove = false;

		for (uint32_t index = 0; index < kNumInstance; ++index)
		{
			particles[index].transform.scale = { 1.0f, 1.0f, 1.0f };
			particles[index].transform.rotate = { 0.0f, 0.0f, 0.0f };
			particles[index].velocity = { 0.0f, 0.02f, 0.0f };
			particles[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };

			if (index < 10) {
				particles[index].transform.translate = { index * 0.1f, index * 0.1f, index * 0.1f };
			} else {
				particles[index].transform.scale = { 0.0f, 0.0f, 0.0f };
			}
		}

		
		struct TornadoParticle {
			float heightRatio;  
			float angle;        
			float speedOffset;  
			float radiusOffset;
			float scale;       
		};

		struct TornadoParams {
			Vector3 center = { 0.0f, -8.0f, 0.0f };        
			float height = 18.0f;                         
			float bottomRadius = 0.8f;                      
			float topRadius = 6.5f;                       
			float rotateSpeed = 0.08f;                     
			float riseSpeed = 0.005f;                    
			float particleScale = 0.8f;                    
			bool isBillboard = true;                      
			Vector4 bottomColor = { 0.2f, 0.7f, 1.0f, 1.0f };
			Vector4 topColor = { 1.0f, 1.0f, 1.0f, 1.0f };  
		} tornadoParams;

		std::vector<TornadoParticle> tornadoParticles(kNumInstance);
		for (uint32_t i = 0; i < kNumInstance; ++i)
		{
			float t = static_cast<float>(i) / static_cast<float>(kNumInstance);
			float rand1 = std::fmod(std::sin(i * 12.9898f) * 43758.5453f, 1.0f);
			if (rand1 < 0.0f) rand1 += 1.0f;
			float rand2 = std::fmod(std::sin(i * 78.233f) * 23421.631f, 1.0f);
			if (rand2 < 0.0f) rand2 += 1.0f;

			tornadoParticles[i].heightRatio = t;                  
			tornadoParticles[i].angle = t * pi_f * 12.0f + rand1;  
			tornadoParticles[i].speedOffset = 0.7f + rand1 * 0.6f;  
			tornadoParticles[i].radiusOffset = (rand2 - 0.5f) * 1.2f;
			tornadoParticles[i].scale = 0.6f + rand2 * 0.8f;       
		}

		
		struct SnowParticle {
			Vector3 basePos;   
			float heightRatio;  
			float fallSpeed;  
			float swayPhase;  
			float swaySpeed;    
			float scale;       
		};

		struct SnowParams {
			Vector3 center = { 0.0f, 0.0f, 0.0f };       
			Vector3 areaSize = { 30.0f, 22.0f, 30.0f };    
			float fallSpeed = 0.004f;                 
			float swayAmplitude = 0.8f;                
			float swayFrequency = 0.03f;                
			Vector3 wind = { 0.01f, 0.0f, 0.0f };      
			float particleScale = 0.28f;               
			bool isBillboard = true;                  
			Vector4 topColor = { 1.0f, 1.0f, 1.0f, 0.95f };
			Vector4 bottomColor = { 0.8f, 0.92f, 1.0f, 0.85f };
		} snowParams;

		std::vector<SnowParticle> snowParticles(kNumInstance);
		for (uint32_t i = 0; i < kNumInstance; ++i)
		{
			auto Hash01 = [](uint32_t seed) -> float {
				float v = std::fmod(std::sin(static_cast<float>(seed) * 12.9898f + 78.233f) * 43758.5453f, 1.0f);
				return (v < 0.0f) ? (v + 1.0f) : v;
				};

			float rx = Hash01(i * 3 + 1) * 2.0f - 1.0f; 
			float ry = Hash01(i * 3 + 2);              
			float rz = Hash01(i * 3 + 3) * 2.0f - 1.0f; 
			float rScale = Hash01(i * 5 + 7);
			float rSpeed = Hash01(i * 7 + 11);

			snowParticles[i].basePos = { rx, 0.0f, rz };
			snowParticles[i].heightRatio = ry;
			snowParticles[i].fallSpeed = 0.6f + rSpeed * 0.8f;   
			snowParticles[i].swayPhase = rScale * pi_f * 2.0f;   
			snowParticles[i].swaySpeed = 0.7f + rSpeed * 0.6f;    
			snowParticles[i].scale = 0.5f + rScale * 0.9f;       
		}
		float snowTime = 0.0f;

	
		struct MeteorParticle {
			Vector3 burstDir;    
			Vector3 trailOffset;  
			float burstSpeed;    
			float scale;        
			float trailLag;     
			float angleRing;    
		};

		struct MeteorParams {
			Vector3 startPos = { -14.0f, 20.0f, -6.0f };
			Vector3 impactPos = { 2.0f, -6.0f, 0.0f }; 
			float cycleSpeed = 0.006f;                    
			float impactTiming = 0.38f;                  
			float explosionRadius = 14.0f;              
			float shockwaveRadius = 18.0f;              
			float gravity = 9.8f;                          
			float particleScale = 0.85f;                    
			bool autoLoop = true;                         
			bool isBillboard = true;                          
			Vector4 coreColor = { 1.0f, 0.98f, 0.85f, 1.0f };  
			Vector4 fireColor = { 1.0f, 0.42f, 0.08f, 0.95f };
			Vector4 smokeColor = { 0.25f, 0.08f, 0.05f, 0.0f };
		} meteorParams;

		std::vector<MeteorParticle> meteorParticles(kNumInstance);
		for (uint32_t i = 0; i < kNumInstance; ++i)
		{
			auto Hash01 = [](uint32_t seed) -> float {
				float v = std::fmod(std::sin(static_cast<float>(seed) * 19.313f + 41.719f) * 43758.5453f, 1.0f);
				return (v < 0.0f) ? (v + 1.0f) : v;
				};
			float u = Hash01(i * 4 + 1);
			float v = Hash01(i * 4 + 2);
			float theta = u * 2.0f * pi_f;
			float phi = std::acos(1.0f - v * 0.92f);

			Vector3 dir = {
				std::sin(phi) * std::cos(theta),
				std::cos(phi),
				std::sin(phi) * std::sin(theta)
			};

			meteorParticles[i].burstDir = Vector3::Normalize(dir);
			meteorParticles[i].trailOffset = {
				(Hash01(i * 6 + 1) - 0.5f) * 2.0f,
				(Hash01(i * 6 + 2) - 0.5f) * 2.0f,
				(Hash01(i * 6 + 3) - 0.5f) * 2.0f
			};
			meteorParticles[i].burstSpeed = 0.35f + Hash01(i * 5 + 3) * 1.05f;
			meteorParticles[i].scale = 0.55f + Hash01(i * 5 + 4) * 0.95f;
			meteorParticles[i].trailLag = static_cast<float>(i) / static_cast<float>(kNumInstance);
			meteorParticles[i].angleRing = (static_cast<float>(i) / 50.0f) * 2.0f * pi_f + Hash01(i) * 0.2f;
		}
		float meteorProgress = 0.0f; 

		struct MagicParticle {
			float angle; 
			float radiusFactor;   
			float lengthFactor;   
			float speedOffset;   
			float scale;        
			Vector3 randomDir;   
		};

		struct MagicParams {
			Vector3 originPos = { -8.0f, 0.5f, -3.0f };    
			Vector3 targetPos = { 10.0f, -1.0f, 5.0f };   
			float circleRadius = 4.2f;                      
			float beamThickness = 1.15f;                     
			float cycleSpeed = 0.0045f;                  
			float fireTiming = 0.36f;                      
			float fadeTiming = 0.80f;           
			float spinSpeed = 0.025f;                  
			float particleScale = 0.42f;              
			bool autoLoop = true;                       
			bool isBillboard = true;                        
			Vector4 coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };  
			Vector4 mainColor = { 0.15f, 0.78f, 1.0f, 0.95f }; 
			Vector4 subColor = { 0.45f, 0.2f, 1.0f, 0.85f };
		} magicParams;

		std::vector<MagicParticle> magicParticles(kNumInstance);
		for (uint32_t i = 0; i < kNumInstance; ++i)
		{
			auto Hash01 = [](uint32_t seed) -> float {
				float v = std::fmod(std::sin(static_cast<float>(seed) * 27.183f + 31.415f) * 43758.5453f, 1.0f);
				return (v < 0.0f) ? (v + 1.0f) : v;
				};

			float u = Hash01(i * 5 + 1);
			float v = Hash01(i * 5 + 2);
			float theta = u * 2.0f * pi_f;
			float phi = std::acos(1.0f - 2.0f * v);

			magicParticles[i].angle = Hash01(i * 3 + 1) * 2.0f * pi_f;
			magicParticles[i].radiusFactor = Hash01(i * 3 + 2);
			magicParticles[i].lengthFactor = static_cast<float>(i) / static_cast<float>(kNumInstance);
			magicParticles[i].speedOffset = 0.65f + Hash01(i * 3 + 3) * 0.75f;
			magicParticles[i].scale = 0.6f + Hash01(i * 7 + 4) * 0.85f;
			magicParticles[i].randomDir = Vector3::Normalize({
				std::sin(phi) * std::cos(theta),
				std::cos(phi),
				std::sin(phi) * std::sin(theta)
				});
		}
		float magicProgress = 0.0f;
		float magicSpinAngle = 0.0f;

		MSG msg{};
		while (msg.message != WM_QUIT)
		{
			if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg); DispatchMessage(&msg);
			} else
			{
				std::memcpy(keyPre, key, sizeof(key));
				keyboard->Acquire(); keyboard->GetDeviceState(sizeof(key), key);
				debugCamera.Update(key);

				if (isAutoRotate) { transformModel.rotate.y += 0.01f; if (transformModel.rotate.y > pi_f * 2.0f) transformModel.rotate.y -= pi_f * 2.0f; }

#ifdef USE_IMGUI
				ImGui_ImplDX12_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::NewFrame();

				ImGui::Begin("Screen & Settings");

				// --- 画面切り替えUI（タブバー形式） ---
				if (ImGui::BeginTabBar("ScreenTabs"))
				{
					if (ImGui::BeginTabItem("3D Model"))
					{
						currentScreen = ScreenMode::Model3D;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Particle"))
					{
						currentScreen = ScreenMode::Particle;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("2D Sprite"))
					{
						currentScreen = ScreenMode::Sprite2D;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Tornado"))
					{
						currentScreen = ScreenMode::Tornado;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Snow"))
					{
						currentScreen = ScreenMode::Snow;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Meteor"))
					{
						currentScreen = ScreenMode::Meteor;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("Magic"))
					{
						currentScreen = ScreenMode::Magic;
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem("All View"))
					{
						currentScreen = ScreenMode::All;
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}

				ImGui::Separator();

				// --- 選択中の画面に応じた設定項目の表示 ---
				if (currentScreen == ScreenMode::Model3D || currentScreen == ScreenMode::All)
				{
					if (ImGui::CollapsingHeader("3D Model Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						const char* modelNames[] = { "Plane", "Sphere", "Fence" };
						ImGui::Combo("Model Select", &currentModelType, modelNames, IM_ARRAYSIZE(modelNames));

						ImGui::DragFloat3("Translate", &transformModel.translate.x, 0.01f);
						ImGui::SliderFloat3("Rotate", &transformModel.rotate.x, -pi_f, pi_f);
						ImGui::DragFloat3("Scale", &transformModel.scale.x, 0.01f);
						ImGui::Checkbox("Auto Rotate", &isAutoRotate);

						ImGui::ColorEdit4("Color", &materialData->color.x);
						if (currentModelType != 2) // Fence以外はテクスチャ切替有効
						{
							ImGui::Checkbox("useMonsterBall Texture", &useMonsterBall);
						}

						const char* lightingItems[] = { "None", "Lambert", "Half Lambert" };
						int currentLighting = materialData->lightingType;
						if (ImGui::Combo("Lighting Mode", &currentLighting, lightingItems, IM_ARRAYSIZE(lightingItems)))
						{
							materialData->lightingType = currentLighting;
							materialData->enableLighting = (currentLighting > 0) ? 1 : 0;
						}
					}
				}

				if (currentScreen == ScreenMode::Particle || currentScreen == ScreenMode::All)
				{
					if (ImGui::CollapsingHeader("Particle Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						if (ImGui::ColorEdit4("All Particles Color", &particleBaseColor.x))
						{
							for (uint32_t i = 0; i < kNumInstance; ++i)
							{
								particles[i].color = particleBaseColor;
							}
						}
						ImGui::ColorEdit4("Particle[0] Color", &particles[0].color.x);
						ImGui::DragFloat3("Particle[0] Pos", &particles[0].transform.translate.x, 0.05f);
						ImGui::DragFloat3("Particle[0] Velocity", &particles[0].velocity.x, 0.005f);
						ImGui::Checkbox("Move by Velocity", &isParticleMove);
					}
				}

				if (currentScreen == ScreenMode::Sprite2D || currentScreen == ScreenMode::All)
				{
					if (ImGui::CollapsingHeader("Sprite Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::SliderFloat2("Sprite Scale", &transformSprite.scale.x, 0.1f, 5.0f);
						ImGui::SliderFloat2("Sprite Translate", &transformSprite.translate.x, -200.0f, 1000.0f);
						ImGui::DragFloat2("UV Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
						ImGui::DragFloat2("UV Scale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
						ImGui::SliderAngle("UV Rotate", &uvTransformSprite.rotate.z);
					}
				}

				if (currentScreen == ScreenMode::Model3D || currentScreen == ScreenMode::All)
				{
					if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::SliderFloat3("Light Direction", &directionalLightData->direction.x, -1.0f, 1.0f);
						ImGui::ColorEdit4("Light Color", &directionalLightData->color.x);
						ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 5.0f);
					}
				}

				if (currentScreen == ScreenMode::Tornado)
				{
					if (ImGui::CollapsingHeader("Tornado Effect Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::DragFloat3("Center Pos", &tornadoParams.center.x, 0.1f);
						ImGui::SliderFloat("Height", &tornadoParams.height, 2.0f, 40.0f);
						ImGui::SliderFloat("Bottom Radius", &tornadoParams.bottomRadius, 0.1f, 10.0f);
						ImGui::SliderFloat("Top Radius", &tornadoParams.topRadius, 0.5f, 20.0f);
						ImGui::SliderFloat("Rotate Speed", &tornadoParams.rotateSpeed, 0.0f, 0.3f);
						ImGui::SliderFloat("Rise Speed", &tornadoParams.riseSpeed, 0.0f, 0.03f);
						ImGui::SliderFloat("Particle Size", &tornadoParams.particleScale, 0.05f, 2.0f);
						ImGui::Checkbox("Billboard (Face Camera)", &tornadoParams.isBillboard);

						ImGui::Separator();
						ImGui::ColorEdit4("Bottom Color", &tornadoParams.bottomColor.x);
						ImGui::ColorEdit4("Top Color", &tornadoParams.topColor.x);
					}
				}

				if (currentScreen == ScreenMode::Snow)
				{
					if (ImGui::CollapsingHeader("Snow Effect Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						ImGui::DragFloat3("Center Pos", &snowParams.center.x, 0.1f);
						ImGui::DragFloat3("Area Size (W, H, D)", &snowParams.areaSize.x, 0.5f, 5.0f, 80.0f);
						ImGui::SliderFloat("Fall Speed", &snowParams.fallSpeed, 0.0f, 0.02f);
						ImGui::SliderFloat("Sway Amplitude", &snowParams.swayAmplitude, 0.0f, 3.0f);
						ImGui::SliderFloat("Sway Frequency", &snowParams.swayFrequency, 0.0f, 0.15f);
						ImGui::SliderFloat3("Wind (X, Y, Z)", &snowParams.wind.x, -0.05f, 0.05f);
						ImGui::SliderFloat("Snow Size", &snowParams.particleScale, 0.05f, 1.5f);
						ImGui::Checkbox("Billboard (Face Camera)", &snowParams.isBillboard);

						ImGui::Separator();
						ImGui::ColorEdit4("Top Color", &snowParams.topColor.x);
						ImGui::ColorEdit4("Bottom Color", &snowParams.bottomColor.x);
					}
				}

				if (currentScreen == ScreenMode::Meteor)
				{
					if (ImGui::CollapsingHeader("Meteor & Explosion Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						if (ImGui::Button("Restart Meteor"))
						{
							meteorProgress = 0.0f;
						}
						ImGui::SameLine();
						ImGui::Checkbox("Auto Loop", &meteorParams.autoLoop);

						ImGui::SliderFloat("Timeline Progress", &meteorProgress, 0.0f, 1.0f);
						ImGui::SliderFloat("Cycle Speed", &meteorParams.cycleSpeed, 0.0f, 0.02f);
						ImGui::SliderFloat("Impact Timing", &meteorParams.impactTiming, 0.15f, 0.7f);

						ImGui::Separator();
						ImGui::DragFloat3("Start Pos (Sky)", &meteorParams.startPos.x, 0.2f);
						ImGui::DragFloat3("Impact Pos (Ground)", &meteorParams.impactPos.x, 0.2f);
						ImGui::SliderFloat("Explosion Radius", &meteorParams.explosionRadius, 3.0f, 30.0f);
						ImGui::SliderFloat("Shockwave Radius", &meteorParams.shockwaveRadius, 3.0f, 35.0f);
						ImGui::SliderFloat("Gravity", &meteorParams.gravity, 0.0f, 25.0f);
						ImGui::SliderFloat("Particle Size", &meteorParams.particleScale, 0.1f, 2.5f);
						ImGui::Checkbox("Billboard (Face Camera)", &meteorParams.isBillboard);

						ImGui::Separator();
						ImGui::ColorEdit4("Core Color", &meteorParams.coreColor.x);
						ImGui::ColorEdit4("Fire Color", &meteorParams.fireColor.x);
						ImGui::ColorEdit4("Smoke Color", &meteorParams.smokeColor.x);
					}
				}

				if (currentScreen == ScreenMode::Magic)
				{
					if (ImGui::CollapsingHeader("Magic Circle & Blast Settings", ImGuiTreeNodeFlags_DefaultOpen))
					{
						if (ImGui::Button("Cast Magic!"))
						{
							magicProgress = 0.0f;
						}
						ImGui::SameLine();
						ImGui::Checkbox("Auto Loop", &magicParams.autoLoop);

						ImGui::Text("Element Presets:");
						if (ImGui::Button("Arcane Blue"))
						{
							magicParams.coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
							magicParams.mainColor = { 0.15f, 0.78f, 1.0f, 0.95f };
							magicParams.subColor = { 0.45f, 0.2f, 1.0f, 0.85f };
						}
						ImGui::SameLine();
						if (ImGui::Button("Crimson Flame"))
						{
							magicParams.coreColor = { 1.0f, 0.96f, 0.75f, 1.0f };
							magicParams.mainColor = { 1.0f, 0.35f, 0.08f, 0.95f };
							magicParams.subColor = { 0.85f, 0.05f, 0.15f, 0.85f };
						}
						ImGui::SameLine();
						if (ImGui::Button("Holy Gold"))
						{
							magicParams.coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
							magicParams.mainColor = { 1.0f, 0.85f, 0.25f, 0.95f };
							magicParams.subColor = { 1.0f, 0.55f, 0.1f, 0.85f };
						}
						ImGui::SameLine();
						if (ImGui::Button("Void Purple"))
						{
							magicParams.coreColor = { 0.98f, 0.85f, 1.0f, 1.0f };
							magicParams.mainColor = { 0.85f, 0.2f, 1.0f, 0.95f };
							magicParams.subColor = { 0.3f, 0.05f, 0.65f, 0.85f };
						}

						ImGui::Separator();
						ImGui::SliderFloat("Timeline Progress", &magicProgress, 0.0f, 1.0f);
						ImGui::SliderFloat("Cast Speed", &magicParams.cycleSpeed, 0.0f, 0.015f);
						ImGui::SliderFloat("Fire Timing", &magicParams.fireTiming, 0.15f, 0.6f);
						ImGui::SliderFloat("Fade Timing", &magicParams.fadeTiming, 0.65f, 0.95f);

						ImGui::Separator();
						ImGui::DragFloat3("Circle Origin", &magicParams.originPos.x, 0.2f);
						ImGui::DragFloat3("Target Pos", &magicParams.targetPos.x, 0.2f);
						ImGui::SliderFloat("Circle Radius", &magicParams.circleRadius, 1.0f, 10.0f);
						ImGui::SliderFloat("Beam Thickness", &magicParams.beamThickness, 0.2f, 3.5f);
						ImGui::SliderFloat("Spin Speed", &magicParams.spinSpeed, 0.0f, 0.1f);
						ImGui::SliderFloat("Particle Size", &magicParams.particleScale, 0.1f, 1.2f);
						ImGui::Checkbox("Billboard (Face Camera)", &magicParams.isBillboard);

						ImGui::Separator();
						ImGui::ColorEdit4("Core Color", &magicParams.coreColor.x);
						ImGui::ColorEdit4("Main Color", &magicParams.mainColor.x);
						ImGui::ColorEdit4("Sub Color", &magicParams.subColor.x);
					}
				}

				ImGui::End();
				ImGui::Render();
#endif

				Matrix4x4 materialTransformMatrix = MakeUVTransformMatrix(uvTransformSprite.scale, uvTransformSprite.rotate.z, uvTransformSprite.translate);
				materialDataSprite->uvTransform = materialTransformMatrix;

				Matrix4x4 viewMatrix = debugCamera.GetMatView();
				Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate);
				Matrix4x4 projectionMatrixModel = Matrix4x4::MakePerspectiveFovMatrix(0.45f, (float)kClientWidth / kClientHeight, 0.1f, 100.0f);
				wvpData->wvp = Matrix4x4::Multiply(Matrix4x4::Multiply(worldMatrixModel, viewMatrix), projectionMatrixModel);
				wvpData->world = worldMatrixModel;

				Matrix4x4 viewProjectionMatrix = Matrix4x4::Multiply(viewMatrix, projectionMatrixModel);

				if (currentScreen == ScreenMode::Tornado)
				{
					// ビルボード用行列（ビュー行列の回転成分の逆行列＝転置行列を作成）
					Matrix4x4 billboardMatrix = Matrix4x4::MakeIdentity4x4();
					if (tornadoParams.isBillboard)
					{
						billboardMatrix = viewMatrix;
						billboardMatrix.m[3][0] = 0.0f;
						billboardMatrix.m[3][1] = 0.0f;
						billboardMatrix.m[3][2] = 0.0f;
						billboardMatrix = Matrix4x4::Transpose(billboardMatrix);
					}

					for (uint32_t index = 0; index < kNumInstance; ++index)
					{
						auto& p = tornadoParticles[index];

						// 1. 高さと回転角度を更新
						p.heightRatio += tornadoParams.riseSpeed * p.speedOffset;
						if (p.heightRatio > 1.0f) {
							p.heightRatio -= 1.0f; // 一番上まで行ったら足元へ戻る
						}
						// 下の方ほど角速度を速くするとリアルな竜巻に見える
						float angularBoost = 1.0f + (1.0f - p.heightRatio) * 1.2f;
						p.angle += tornadoParams.rotateSpeed * p.speedOffset * angularBoost;

						// 2. 高さに応じた半径（下は細く、上に行くほど広がる）
						float currentRadius = tornadoParams.bottomRadius
							+ (tornadoParams.topRadius - tornadoParams.bottomRadius) * std::pow(p.heightRatio, 1.3f)
							+ p.radiusOffset * (0.3f + p.heightRatio);
						if (currentRadius < 0.1f) currentRadius = 0.1f;

						// 3. 3D空間上の座標を計算
						Vector3 pos;
						pos.x = tornadoParams.center.x + std::cos(p.angle) * currentRadius;
						pos.y = tornadoParams.center.y + p.heightRatio * tornadoParams.height;
						pos.z = tornadoParams.center.z + std::sin(p.angle) * currentRadius;

						// 4. 上端・下端で自然に小さく消えるようにスケール調整（フェードイン・アウト）
						float edgeFade = std::sin(p.heightRatio * pi_f);
						float s = tornadoParams.particleScale * p.scale * (0.2f + 0.8f * edgeFade);

						// 5. ワールド行列の組み立て（Scale * Billboard * Translate）
						Matrix4x4 scaleMat = Matrix4x4::MakeScaleMatrix({ s, s, s });
						Matrix4x4 transMat = Matrix4x4::MakeTranslateMatrix(pos);
						Matrix4x4 worldMatrix = Matrix4x4::Multiply(Matrix4x4::Multiply(scaleMat, billboardMatrix), transMat);

						// 6. 高さに応じたカラーグラデーション
						float t = p.heightRatio;
						instancingData[index].color = {
							tornadoParams.bottomColor.x + (tornadoParams.topColor.x - tornadoParams.bottomColor.x) * t,
							tornadoParams.bottomColor.y + (tornadoParams.topColor.y - tornadoParams.bottomColor.y) * t,
							tornadoParams.bottomColor.z + (tornadoParams.topColor.z - tornadoParams.bottomColor.z) * t,
							tornadoParams.bottomColor.w + (tornadoParams.topColor.w - tornadoParams.bottomColor.w) * t
						};

						instancingData[index].world = worldMatrix;
						instancingData[index].wvp = Matrix4x4::Multiply(worldMatrix, viewProjectionMatrix);
					}
				} else if (currentScreen == ScreenMode::Snow)
				{
					// 雪（Snow）演出の更新処理
					snowTime += 1.0f;

					Matrix4x4 billboardMatrix = Matrix4x4::MakeIdentity4x4();
					if (snowParams.isBillboard)
					{
						billboardMatrix = viewMatrix;
						billboardMatrix.m[3][0] = 0.0f;
						billboardMatrix.m[3][1] = 0.0f;
						billboardMatrix.m[3][2] = 0.0f;
						billboardMatrix = Matrix4x4::Transpose(billboardMatrix);
					}

					for (uint32_t index = 0; index < kNumInstance; ++index)
					{
						auto& p = snowParticles[index];

						// 1. 落下（heightRatio を 1.0 → 0.0 へ減少させる）
						p.heightRatio -= snowParams.fallSpeed * p.fallSpeed;
						if (p.heightRatio < 0.0f) {
							p.heightRatio += 1.0f; // 地面まで落ちたら再び上空へ
						}

						// 2. 風による横移動（-1.0 〜 1.0 の範囲でループ）
						if (snowParams.areaSize.x > 0.0f) {
							p.basePos.x += (snowParams.wind.x / (snowParams.areaSize.x * 0.5f));
							if (p.basePos.x > 1.0f) p.basePos.x -= 2.0f;
							if (p.basePos.x < -1.0f) p.basePos.x += 2.0f;
						}
						if (snowParams.areaSize.z > 0.0f) {
							p.basePos.z += (snowParams.wind.z / (snowParams.areaSize.z * 0.5f));
							if (p.basePos.z > 1.0f) p.basePos.z -= 2.0f;
							if (p.basePos.z < -1.0f) p.basePos.z += 2.0f;
						}

						// 3. ひらひらとした横揺れ（sin / cos）を計算
						float phase = snowTime * snowParams.swayFrequency * p.swaySpeed + p.swayPhase;
						float swayX = std::sin(phase) * snowParams.swayAmplitude;
						float swayZ = std::cos(phase * 0.8f) * snowParams.swayAmplitude * 0.6f;

						// 4. 3D空間上の座標を計算
						Vector3 pos;
						pos.x = snowParams.center.x + p.basePos.x * (snowParams.areaSize.x * 0.5f) + swayX;
						pos.y = snowParams.center.y + (p.heightRatio - 0.5f) * snowParams.areaSize.y;
						pos.z = snowParams.center.z + p.basePos.z * (snowParams.areaSize.z * 0.5f) + swayZ;

						// 5. 出現時（上端）と消滅時（地面付近）で自然にフェードするサイズ調整
						float edgeFade = std::sin(p.heightRatio * pi_f);
						float s = snowParams.particleScale * p.scale * (0.2f + 0.8f * edgeFade);

						// 6. ワールド行列の組み立て（Scale * Billboard * Translate）
						Matrix4x4 scaleMat = Matrix4x4::MakeScaleMatrix({ s, s, s });
						Matrix4x4 transMat = Matrix4x4::MakeTranslateMatrix(pos);
						Matrix4x4 worldMatrix = Matrix4x4::Multiply(Matrix4x4::Multiply(scaleMat, billboardMatrix), transMat);

						// 7. 高さに応じたカラー補間（上は純白、下は淡い氷色）
						float t = p.heightRatio;
						instancingData[index].color = {
							snowParams.bottomColor.x + (snowParams.topColor.x - snowParams.bottomColor.x) * t,
							snowParams.bottomColor.y + (snowParams.topColor.y - snowParams.bottomColor.y) * t,
							snowParams.bottomColor.z + (snowParams.topColor.z - snowParams.bottomColor.z) * t,
							snowParams.bottomColor.w + (snowParams.topColor.w - snowParams.bottomColor.w) * t
						};

						instancingData[index].world = worldMatrix;
						instancingData[index].wvp = Matrix4x4::Multiply(worldMatrix, viewProjectionMatrix);
					}
				} else if (currentScreen == ScreenMode::Meteor)
				{
					// ==========================================
					// 隕石からの爆発（Meteor & Explosion）更新処理
					// ==========================================
					meteorProgress += meteorParams.cycleSpeed;
					if (meteorProgress > 1.0f)
					{
						if (meteorParams.autoLoop) {
							meteorProgress -= 1.0f;
						} else {
							meteorProgress = 1.0f;
						}
					}

					Matrix4x4 billboardMatrix = Matrix4x4::MakeIdentity4x4();
					if (meteorParams.isBillboard)
					{
						billboardMatrix = viewMatrix;
						billboardMatrix.m[3][0] = 0.0f;
						billboardMatrix.m[3][1] = 0.0f;
						billboardMatrix.m[3][2] = 0.0f;
						billboardMatrix = Matrix4x4::Transpose(billboardMatrix);
					}

					Vector3 fallVec = Vector3::Subtract(meteorParams.impactPos, meteorParams.startPos);

					for (uint32_t index = 0; index < kNumInstance; ++index)
					{
						auto& p = meteorParticles[index];
						Vector3 pos = meteorParams.impactPos;
						float s = 0.0f;
						Vector4 col = meteorParams.coreColor;

						if (meteorProgress < meteorParams.impactTiming)
						{
							// フェーズ1：隕石の落下（コア ＋ 長い炎の尾）
							float fallRatio = meteorProgress / meteorParams.impactTiming;
							float easedFall = std::pow(fallRatio, 1.35f);

							if (index < kNumInstance / 10)
							{
								float coreSpread = 0.35f;
								pos = Vector3::Add(meteorParams.startPos, Vector3::Multiply(easedFall, fallVec));
								pos = Vector3::Add(pos, Vector3::Multiply(coreSpread, p.trailOffset));

								s = meteorParams.particleScale * (1.2f + p.scale * 0.6f);
								col = LerpColor(meteorParams.fireColor, meteorParams.coreColor, 0.75f);
							} else
							{
								float lag = p.trailLag * 0.32f;
								float tTrail = easedFall - lag;
								if (tTrail < 0.0f)
								{
									s = 0.0f;
								} else
								{
									float spread = p.trailLag * 2.2f;
									pos = Vector3::Add(meteorParams.startPos, Vector3::Multiply(tTrail, fallVec));
									pos = Vector3::Add(pos, Vector3::Multiply(spread, p.trailOffset));

									s = meteorParams.particleScale * p.scale * (1.1f - p.trailLag * 0.7f);
									if (p.trailLag < 0.45f) {
										col = LerpColor(meteorParams.coreColor, meteorParams.fireColor, p.trailLag / 0.45f);
									} else {
										col = LerpColor(meteorParams.fireColor, meteorParams.smokeColor, (p.trailLag - 0.45f) / 0.55f);
									}
								}
							}
						} else
						{
							// フェーズ2：着弾・大爆発（爆心 ＋ 衝撃波 ＋ 火の粉）
							float expRatio = (meteorProgress - meteorParams.impactTiming) / (1.0f - meteorParams.impactTiming);
							float easeOut = 1.0f - std::pow(1.0f - expRatio, 2.6f);

							uint32_t coreLimit = kNumInstance * 3 / 10;
							uint32_t ringLimit = kNumInstance * 11 / 20;

							if (index < coreLimit)
							{
								float dist = meteorParams.explosionRadius * 0.55f * easeOut * p.burstSpeed;
								pos = Vector3::Add(meteorParams.impactPos, Vector3::Multiply(dist, p.burstDir));
								pos.y += expRatio * 2.5f;

								float pulse = std::sin(Clamp01(expRatio * 1.15f) * pi_f);
								s = meteorParams.particleScale * (1.4f + p.scale) * (0.25f + 1.15f * pulse);

								if (expRatio < 0.25f) {
									col = LerpColor(meteorParams.coreColor, meteorParams.fireColor, expRatio / 0.25f);
								} else {
									col = LerpColor(meteorParams.fireColor, meteorParams.smokeColor, (expRatio - 0.25f) / 0.75f);
								}
							} else if (index < ringLimit)
							{
								float ringR = meteorParams.shockwaveRadius * easeOut;
								pos.x = meteorParams.impactPos.x + std::cos(p.angleRing) * ringR;
								pos.y = meteorParams.impactPos.y + 0.2f + expRatio * 0.6f;
								pos.z = meteorParams.impactPos.z + std::sin(p.angleRing) * ringR;

								s = meteorParams.particleScale * 0.9f * (1.0f - expRatio * 0.6f);
								col = LerpColor(meteorParams.coreColor, meteorParams.fireColor, expRatio);
								col.w = (1.0f - expRatio) * 0.85f;
							} else
							{
								float dist = meteorParams.explosionRadius * 1.25f * easeOut * p.burstSpeed;
								pos = Vector3::Add(meteorParams.impactPos, Vector3::Multiply(dist, p.burstDir));
								pos.y -= 0.5f * meteorParams.gravity * (expRatio * expRatio);
								if (pos.y < meteorParams.impactPos.y) {
									pos.y = meteorParams.impactPos.y;
								}

								s = meteorParams.particleScale * 0.55f * p.scale * (1.0f - expRatio * 0.75f);
								if (expRatio < 0.4f) {
									col = LerpColor(meteorParams.coreColor, meteorParams.fireColor, expRatio / 0.4f);
								} else {
									col = LerpColor(meteorParams.fireColor, meteorParams.smokeColor, (expRatio - 0.4f) / 0.6f);
								}
							}
						}

						Matrix4x4 scaleMat = Matrix4x4::MakeScaleMatrix({ s, s, s });
						Matrix4x4 transMat = Matrix4x4::MakeTranslateMatrix(pos);
						Matrix4x4 worldMatrix = Matrix4x4::Multiply(Matrix4x4::Multiply(scaleMat, billboardMatrix), transMat);

						instancingData[index].color = col;
						instancingData[index].world = worldMatrix;
						instancingData[index].wvp = Matrix4x4::Multiply(worldMatrix, viewProjectionMatrix);
					}
				} else if (currentScreen == ScreenMode::Magic)
				{
					magicProgress += magicParams.cycleSpeed;
					if (magicProgress > 1.0f)
					{
						if (magicParams.autoLoop) {
							magicProgress -= 1.0f;
						} else {
							magicProgress = 1.0f;
						}
					}

					// 発射中は回転を速くして魔力出力を表現
					float spinMultiplier = (magicProgress >= magicParams.fireTiming && magicProgress < magicParams.fadeTiming) ? 2.8f : 1.0f;
					magicSpinAngle += magicParams.spinSpeed * spinMultiplier;

					Matrix4x4 billboardMatrix = Matrix4x4::MakeIdentity4x4();
					if (magicParams.isBillboard)
					{
						billboardMatrix = viewMatrix;
						billboardMatrix.m[3][0] = 0.0f;
						billboardMatrix.m[3][1] = 0.0f;
						billboardMatrix.m[3][2] = 0.0f;
						billboardMatrix = Matrix4x4::Transpose(billboardMatrix);
					}

					// 発射方向ベクトル（forwardVec）と、魔法陣平面の基底（rightVec, upVec）を計算
					Vector3 diff = Vector3::Subtract(magicParams.targetPos, magicParams.originPos);
					float beamLength = Vec3Length(diff);
					Vector3 forwardVec = (beamLength > 0.001f) ? Vector3::Normalize(diff) : Vector3{ 1.0f, 0.0f, 0.0f };
					Vector3 worldUp = (std::abs(forwardVec.y) > 0.95f) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
					Vector3 rightVec = Vector3::Normalize(Vec3Cross(worldUp, forwardVec));
					Vector3 upVec = Vector3::Normalize(Vec3Cross(forwardVec, rightVec));

					// 魔法陣全体の展開スケール（最初0.15秒でパッと広がり、最後0.20秒で消えていく）
					float circleOpen = Clamp01(magicProgress / 0.14f);
					float circleClose = 1.0f - Clamp01((magicProgress - magicParams.fadeTiming) / (1.0f - magicParams.fadeTiming));
					float circleEnvelope = std::sin(circleOpen * (pi_f * 0.5f)) * circleClose;
					float currentCircleR = magicParams.circleRadius * circleEnvelope;

					// 魔法陣平面上の2D座標 (lx, ly) と前後の厚み lz からワールド座標を作るラムダ
					auto ToMagicWorld = [&](float lx, float ly, float lz) -> Vector3 {
						Vector3 p = magicParams.originPos;
						p = Vector3::Add(p, Vector3::Multiply(lx, rightVec));
						p = Vector3::Add(p, Vector3::Multiply(ly, upVec));
						p = Vector3::Add(p, Vector3::Multiply(lz, forwardVec));
						return p;
						};

					// タイムラインのフェーズ判定
					bool isCharging = (magicProgress < magicParams.fireTiming);
					bool isFiring = (magicProgress >= magicParams.fireTiming && magicProgress < magicParams.fadeTiming);
					float chargeRatio = Clamp01(magicProgress / magicParams.fireTiming);
					float fireRatio = isFiring ? Clamp01((magicProgress - magicParams.fireTiming) / (magicParams.fadeTiming - magicParams.fireTiming)) : 0.0f;
					float afterRatio = (magicProgress >= magicParams.fadeTiming) ? Clamp01((magicProgress - magicParams.fadeTiming) / (1.0f - magicParams.fadeTiming)) : 0.0f;

					for (uint32_t index = 0; index < kNumInstance; ++index)
					{
						auto& p = magicParticles[index];
						Vector3 pos = magicParams.originPos;
						float s = 0.0f;
						Vector4 col = magicParams.mainColor;

						if (index < 180)
						{
							// --------------------------------------------------
							// グループ1 (0〜179): 魔法陣の外周二重リング ＋ ルーンノード
							// --------------------------------------------------
							uint32_t localIdx = index;
							bool isOuter = (localIdx < 110);
							float t = isOuter ? (static_cast<float>(localIdx) / 110.0f) : (static_cast<float>(localIdx - 110) / 70.0f);
							float ringR = isOuter ? currentCircleR : (currentCircleR * 0.78f);
							float dirSign = isOuter ? 1.0f : -1.2f;
							float theta = t * 2.0f * pi_f + magicSpinAngle * dirSign;

							float lx = std::cos(theta) * ringR;
							float ly = std::sin(theta) * ringR;
							float lz = isOuter ? 0.0f : 0.25f;

							pos = ToMagicWorld(lx, ly, lz);

							bool isRuneNode = isOuter && (localIdx % 11 == 0);
							float nodeBoost = isRuneNode ? 1.85f : 0.85f;
							s = magicParams.particleScale * nodeBoost * circleEnvelope;

							col = isRuneNode
								? LerpColor(magicParams.mainColor, magicParams.coreColor, 0.75f)
								: (isOuter ? magicParams.mainColor : magicParams.subColor);
							col.w *= circleClose;
						} else if (index < 320)
						{
							// --------------------------------------------------
							// グループ2 (180〜319): 回転する六芒星（ヘキサグラム）＆前方ゲートリング
							// --------------------------------------------------
							uint32_t localIdx = index - 180; // 0〜139
							if (localIdx < 108)
							{
								// 2つの正三角形（各54粒 = 1辺18粒）で構成する六芒星
								uint32_t triIdx = localIdx / 54;        // 0 or 1
								uint32_t inTri = localIdx % 54;         // 0〜53
								uint32_t edgeIdx = inTri / 18;          // 0, 1, 2
								float edgeT = static_cast<float>(inTri % 18) / 18.0f;

								float baseRot = magicSpinAngle * (triIdx == 0 ? 0.8f : -0.8f) + (triIdx == 1 ? pi_f : 0.0f);
								float a0 = baseRot + edgeIdx * (2.0f * pi_f / 3.0f);
								float a1 = baseRot + ((edgeIdx + 1) % 3) * (2.0f * pi_f / 3.0f);

								float starR = currentCircleR * 0.76f;
								float x0 = std::cos(a0) * starR, y0 = std::sin(a0) * starR;
								float x1 = std::cos(a1) * starR, y1 = std::sin(a1) * starR;

								float lx = x0 + (x1 - x0) * edgeT;
								float ly = y0 + (y1 - y0) * edgeT;
								pos = ToMagicWorld(lx, ly, 0.12f);

								s = magicParams.particleScale * 0.75f * circleEnvelope;
								col = LerpColor(magicParams.subColor, magicParams.coreColor, isFiring ? 0.55f : 0.25f);
								col.w *= circleClose;
							} else
							{
								// 前方に浮かぶ加速収束リング（32粒）
								float t = static_cast<float>(localIdx - 108) / 32.0f;
								float theta = t * 2.0f * pi_f - magicSpinAngle * 2.0f;
								float gateR = currentCircleR * 0.42f * (isFiring ? (1.0f + 0.15f * std::sin(fireRatio * 20.0f)) : 1.0f);
								float gateZ = 1.6f * circleEnvelope;

								pos = ToMagicWorld(std::cos(theta) * gateR, std::sin(theta) * gateR, gateZ);
								s = magicParams.particleScale * 0.85f * circleEnvelope;
								col = LerpColor(magicParams.mainColor, magicParams.coreColor, 0.6f);
								col.w *= circleClose;
							}
						} else if (index < 420)
						{
							// --------------------------------------------------
							// グループ3 (320〜419): マナ収束（チャージ）＆二重螺旋エネルギー
							// --------------------------------------------------
							uint32_t localIdx = index - 320; // 0〜99
							float u = static_cast<float>(localIdx) / 100.0f;

							if (isCharging)
							{
								// 外周から中心コアへ螺旋を描きながら吸い込まれるマナ粒子
								float loopT = std::fmod(chargeRatio * 2.2f * p.speedOffset + u, 1.0f);
								float r = magicParams.circleRadius * 1.45f * (1.0f - loopT);
								float theta = p.angle + loopT * pi_f * 4.0f;
								float lz = -1.5f * (1.0f - loopT);

								pos = ToMagicWorld(std::cos(theta) * r, std::sin(theta) * r, lz);
								s = magicParams.particleScale * p.scale * std::sin(loopT * pi_f) * 1.1f;
								col = LerpColor(magicParams.subColor, magicParams.coreColor, loopT);
							} else if (isFiring)
							{
								// ビームの周囲を高速で巻き付きながら進む二重螺旋（ダブルヘリックス）
								float strand = (localIdx % 2 == 0) ? 0.0f : pi_f;
								float beamFront = Clamp01(fireRatio / 0.16f); // 最初の瞬間にターゲットまで到達
								float zDist = u * beamLength * beamFront;
								float helixAngle = u * pi_f * 10.0f - fireRatio * 35.0f + strand;
								float helixR = magicParams.beamThickness * (1.25f - 0.3f * u);

								pos = ToMagicWorld(std::cos(helixAngle) * helixR, std::sin(helixAngle) * helixR, zDist);
								s = magicParams.particleScale * 0.95f * (1.0f - u * 0.3f);
								col = LerpColor(magicParams.coreColor, magicParams.mainColor, u * 0.7f);
							} else
							{
								// 余韻フェーズ
								float drift = afterRatio * 3.5f * p.speedOffset;
								pos = Vector3::Add(magicParams.originPos, Vector3::Multiply(u * beamLength, forwardVec));
								pos = Vector3::Add(pos, Vector3::Multiply(drift, p.randomDir));
								s = magicParams.particleScale * 0.7f * (1.0f - afterRatio);
								col = magicParams.subColor;
								col.w *= (1.0f - afterRatio);
							}
						} else
						{
							// --------------------------------------------------
							// グループ4 (420〜599): 中心コア → アルカナビーム本体 ＆ 着弾バースト
							// --------------------------------------------------
							uint32_t localIdx = index - 420; // 0〜179
							if (isCharging)
							{
								// 魔法陣の中心で徐々に膨張・白熱する魔力コア
								float corePulse = 0.25f + 0.75f * std::pow(chargeRatio, 1.4f);
								float coreR = magicParams.beamThickness * 0.85f * corePulse * p.radiusFactor;
								pos = Vector3::Add(magicParams.originPos, Vector3::Multiply(coreR, p.randomDir));
								pos = Vector3::Add(pos, Vector3::Multiply(0.4f, forwardVec));

								s = magicParams.particleScale * p.scale * (0.4f + 1.1f * corePulse);
								col = LerpColor(magicParams.mainColor, magicParams.coreColor, chargeRatio);
							} else if (isFiring)
							{
								float beamFront = Clamp01(fireRatio / 0.16f);
								// 終盤(fireRatio > 0.75)に向けてビームが少し細くなり始める
								float beamDecay = 1.0f - Clamp01((fireRatio - 0.72f) / 0.28f) * 0.55f;

								if (localIdx < 120)
								{
									// 極太魔導ビーム本体（120粒：中心の白熱芯 ＋ 外側のオーラ層）
									float tBeam = static_cast<float>(localIdx) / 120.0f;
									float zDist = tBeam * beamLength * beamFront;
									bool isInnerCore = (localIdx % 3 == 0);
									float radial = (isInnerCore ? 0.25f : 0.95f) * magicParams.beamThickness * beamDecay * p.radiusFactor;
									float theta = p.angle + fireRatio * 18.0f;

									pos = ToMagicWorld(std::cos(theta) * radial, std::sin(theta) * radial, zDist);
									s = magicParams.particleScale * (isInnerCore ? 1.55f : 1.15f) * beamDecay;
									col = isInnerCore
										? magicParams.coreColor
										: LerpColor(magicParams.coreColor, magicParams.mainColor, 0.65f);
								} else
								{
									// 着弾地点（targetPos）での魔力爆発バースト（60粒）
									if (beamFront < 0.85f)
									{
										s = 0.0f; // ビーム到達前は非表示
									} else
									{
										float burstT = std::fmod((fireRatio - 0.14f) * 1.8f * p.speedOffset + p.radiusFactor, 1.0f);
										float burstDist = magicParams.circleRadius * 1.4f * burstT;
										pos = Vector3::Add(magicParams.targetPos, Vector3::Multiply(burstDist, p.randomDir));

										s = magicParams.particleScale * p.scale * 1.35f * (1.0f - burstT);
										col = LerpColor(magicParams.coreColor, magicParams.mainColor, burstT);
										col.w = (1.0f - burstT) * 0.95f;
									}
								}
							} else
							{
								// 余韻フェーズ
								float spread = afterRatio * 4.5f * p.speedOffset;
								Vector3 anchor = (localIdx < 120)
									? Vector3::Add(magicParams.originPos, Vector3::Multiply((static_cast<float>(localIdx) / 120.0f) * beamLength, forwardVec))
									: magicParams.targetPos;
								pos = Vector3::Add(anchor, Vector3::Multiply(spread, p.randomDir));
								s = magicParams.particleScale * 0.85f * (1.0f - afterRatio);
								col = LerpColor(magicParams.mainColor, magicParams.subColor, afterRatio);
								col.w = (1.0f - afterRatio) * 0.8f;
							}
						}

						// ワールド行列の組み立て（Scale * Billboard * Translate）
						Matrix4x4 scaleMat = Matrix4x4::MakeScaleMatrix({ s, s, s });
						Matrix4x4 transMat = Matrix4x4::MakeTranslateMatrix(pos);
						Matrix4x4 worldMatrix = Matrix4x4::Multiply(Matrix4x4::Multiply(scaleMat, billboardMatrix), transMat);

						instancingData[index].color = col;
						instancingData[index].world = worldMatrix;
						instancingData[index].wvp = Matrix4x4::Multiply(worldMatrix, viewProjectionMatrix);
					}
				} else
				{
					// Particle構造体配列更新
					for (uint32_t index = 0; index < kNumInstance; ++index)
					{
						if (isParticleMove)
						{
							particles[index].transform.translate = Vector3::Add(particles[index].transform.translate, particles[index].velocity);
						}

						Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(
							particles[index].transform.scale,
							particles[index].transform.rotate,
							particles[index].transform.translate
						);
						Matrix4x4 worldViewProjectionMatrix = Matrix4x4::Multiply(worldMatrix, viewProjectionMatrix);
						instancingData[index].wvp = worldViewProjectionMatrix;
						instancingData[index].world = worldMatrix;
						instancingData[index].color = particles[index].color;
					}
				}

				Matrix4x4 worldMatrixSprite = Matrix4x4::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
				Matrix4x4 projectionMatrixSprite = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 100.0f);
				transformationMatrixDataSprite->wvp = Matrix4x4::Multiply(worldMatrixSprite, projectionMatrixSprite);
				transformationMatrixDataSprite->world = worldMatrixSprite;

				commandAllocator->Reset();
				commandList->Reset(commandAllocator.Get(), nullptr);
				UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

				D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
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

				// 1. 3Dモデル描画
				if (currentScreen == ScreenMode::Model3D || currentScreen == ScreenMode::All)
				{
					commandList->SetGraphicsRootSignature(rootSignature.Get());
					commandList->SetPipelineState(pipelineState.Get());
					commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

					if (currentModelType == 0) // Plane
					{
						commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU1);
						commandList->IASetVertexBuffers(0, 1, &vertexBufferViewPlane);
						commandList->DrawInstanced(static_cast<UINT>(planeModelData.vertices.size()), 1, 0, 0);
					} else if (currentModelType == 1) // Sphere
					{
						commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU1);
						commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);
						commandList->DrawInstanced(static_cast<UINT>(sphereModelData.vertices.size()), 1, 0, 0);
					} else if (currentModelType == 2) // Fence
					{
						commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU3);
						commandList->IASetVertexBuffers(0, 1, &vertexBufferViewFence);
						commandList->DrawInstanced(static_cast<UINT>(fenceModelData.vertices.size()), 1, 0, 0);
					}
				}

				// 2. パーティクル描画 (Particle / Tornado / Snow / Meteor / Magic / All 画面のとき)
				if (currentScreen == ScreenMode::Particle || currentScreen == ScreenMode::All || currentScreen == ScreenMode::Tornado || currentScreen == ScreenMode::Snow || currentScreen == ScreenMode::Meteor || currentScreen == ScreenMode::Magic)
				{
					commandList->SetGraphicsRootSignature(particleRootSignature.Get());
					commandList->SetPipelineState(particlePipelineState.Get());
					commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU5);
					commandList->IASetVertexBuffers(0, 1, &vertexBufferViewParticle);
					commandList->DrawInstanced(static_cast<UINT>(particleModelData.vertices.size()), kNumInstance, 0, 0);
				}

				// 3. 2Dスプライト描画
				if (currentScreen == ScreenMode::Sprite2D || currentScreen == ScreenMode::All)
				{
					commandList->SetGraphicsRootSignature(rootSignature.Get());
					commandList->SetPipelineState(pipelineState.Get());
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
				fenceValue++; commandQueue->Signal(fence.Get(), fenceValue);
				if (fence->GetCompletedValue() < fenceValue)
				{
					fence->SetEventOnCompletion(fenceValue, fenceEvent);
					WaitForSingleObject(fenceEvent, INFINITE);
				}
			}
		}

#ifdef USE_IMGUI
		ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
#endif

		if (keyboard) { keyboard->Unacquire(); keyboard->Release(); }
		if (directInput) directInput->Release();
		xAudio2.Reset(); SoundUnload(&soundData1);

		instancingResource->Unmap(0, nullptr);
		directionalLightResource->Unmap(0, nullptr); materialResourceSprite->Unmap(0, nullptr);
		transformationMatrixResourceSprite->Unmap(0, nullptr); wvpResource->Unmap(0, nullptr); materialResource->Unmap(0, nullptr);
		CloseHandle(fenceEvent);
	}

	UnregisterClass(wc.lpszClassName, wc.hInstance);
	CoUninitialize();
	return 0;
}