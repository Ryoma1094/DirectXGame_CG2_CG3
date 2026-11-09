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

#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

#include <dxcapi.h>
#include <dxgidebug.h>

#include "Vector3.h"
#include "Matrix4x4.h"

#include "DebugCamera.h"
#include "Input.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "StringUtility.h"
#include "Logger.h"

#pragma comment(lib,"Windowscodecs.lib")

#ifndef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

struct ChunkHeader { char id[4]; int32_t size; };
struct RiffHeader { ChunkHeader chunk; char type[4]; };
struct FormatChunk { ChunkHeader chunk; WAVEFORMATEX fmt; };
struct SoundData { WAVEFORMATEX wfex; BYTE* pBuffer; unsigned int bufferSize; };

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
	char* pBuffer = new char[data.size]; file.read(pBuffer, data.size);
	file.close();
	return { format.fmt, reinterpret_cast<BYTE*>(pBuffer), static_cast<unsigned int>(data.size) };
}

void SoundUnload(SoundData* soundData) { delete[] soundData->pBuffer; soundData->pBuffer = nullptr; }
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData) {
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	if (SUCCEEDED(xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex))) {
		XAUDIO2_BUFFER buf{}; buf.pAudioData = soundData.pBuffer; buf.AudioBytes = soundData.bufferSize; buf.Flags = XAUDIO2_END_OF_STREAM;
		pSourceVoice->SubmitSourceBuffer(&buf); pSourceVoice->Start();
	}
}

struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
		}
	}
};

struct Vector2 { float x, y; };
struct Vector4 { float x, y, z, w; };
struct Vertex { Vector4 position; Vector2 texcoord; Vector3 normal; };

struct MaterialData { std::string textureFilePath; };
struct ModelData { std::vector<Vertex> vertices; MaterialData material; };

struct Material {
	Vector4 color;
	int32_t enableLighting; // 0: Disable, 1: Enable
	int32_t lightingType;   // 0: None, 1: Lambert, 2: Half Lambert
	float padding[2];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix { Matrix4x4 wvp; Matrix4x4 world; };
struct Transform {
	Vector3 scale{ 1.0f, 1.0f, 1.0f };
	Vector3 rotate{ 0.0f, 0.0f, 0.0f };
	Vector3 translate{ 0.0f, 0.0f, 0.0f };
};
struct DirectionalLight { Vector4 color; Vector3 direction; float intensity; };

Matrix4x4 MakeUVTransformMatrix(const Vector3& scale, float rotateZ, const Vector3& translate) {
	Matrix4x4 mScale = Matrix4x4::MakeScaleMatrix({ scale.x, scale.y, 1.0f });
	Matrix4x4 mRotZ = Matrix4x4::MakeRotateZMatrix(rotateZ);
	Matrix4x4 mTranslate = Matrix4x4::MakeTranslateMatrix({ translate.x, translate.y, 0.0f });
	return Matrix4x4::Multiply(Matrix4x4::Multiply(mScale, mRotZ), mTranslate);
}

ModelData CreateSphereModel(uint32_t kSubdivision = 32) {
	ModelData modelData;
	const float kPI = 3.1415926535f;
	for (uint32_t latIndex = latIndex = 0; latIndex < kSubdivision; ++latIndex) {
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

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile, DirectXCommon* dxCommon) {
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	dxCommon->GetDxcUtils()->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	DxcBuffer shaderSourceBuffer{ shaderSource->GetBufferPointer(), shaderSource->GetBufferSize(), DXC_CP_UTF8 };
	LPCWSTR arguments[] = { filePath.c_str(), L"-E", L"main", L"-T", profile, L"-Zi", L"-Qembed_debug", L"-Od", L"-Zpr", L"-HV", L"2021" };
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	dxCommon->GetDxcCompiler()->Compile(&shaderSourceBuffer, arguments, _countof(arguments), dxCommon->GetIncludeHandler(), IID_PPV_ARGS(&shaderResult));
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	return shaderBlob;
}

void LoadTextureFile(const std::wstring& filePath, std::vector<uint8_t>& outData, D3D12_RESOURCE_DESC& outDesc) {
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

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	std::vector<Vector4> positions; std::vector<Vector3> normals; std::vector<Vector2> texcoords;
	std::string line; std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());
	while (std::getline(file, line)) {
		std::string identifier; std::istringstream s(line); s >> identifier;
		if (identifier == "v") { Vector4 pos; s >> pos.x >> pos.y >> pos.z; pos.w = 1.0f; positions.push_back(pos); } else if (identifier == "vt") { Vector2 tex; s >> tex.x >> tex.y; tex.y = 1.0f - tex.y; texcoords.push_back(tex); } else if (identifier == "vn") { Vector3 norm; s >> norm.x >> norm.y >> norm.z; normals.push_back(norm); } else if (identifier == "f") {
			Vertex triangle[3];
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition; s >> vertexDefinition; std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3] = { 0,0,0 };
				for (int32_t element = 0; element < 3; ++element) {
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

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	D3DResourceLeakChecker leakCheck;

	// WinAppクラスを使った初期化
	WinApp* winApp = new WinApp();
	winApp->Initialize();

	// DirectXCommonクラスの生成と初期化
	DirectXCommon* dxCommon = new DirectXCommon();
	dxCommon->Initialize(winApp);

	const int32_t kClientWidth = WinApp::kClientWidth;
	const int32_t kClientHeight = WinApp::kClientHeight;

	{
		// --- 入力の初期化 ---
		Input* input = new Input();
		input->Initialize(winApp);

		// XAudio2
		Microsoft::WRL::ComPtr<IXAudio2> xAudio2; IXAudio2MasteringVoice* masterVoice = nullptr;
		XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR); xAudio2->CreateMasteringVoice(&masterVoice);
		SoundData soundData1 = SoundLoadWave("resources/fanfare.wav"); SoundPlayWave(xAudio2.Get(), soundData1);

		D3D12_ROOT_PARAMETER rootParameters[4] = {};
		rootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[0].Descriptor.ShaderRegister = 0;
		rootParameters[1] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_VERTEX }; rootParameters[1].Descriptor.ShaderRegister = 1;
		D3D12_DESCRIPTOR_RANGE descriptorRange[1] = { {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND} };
		rootParameters[2] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[2].DescriptorTable = { 1, descriptorRange };
		rootParameters[3] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL }; rootParameters[3].Descriptor.ShaderRegister = 1;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = { {D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0, 0, D3D12_COMPARISON_FUNC_NEVER, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, D3D12_FLOAT32_MAX, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL} };

		D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{ _countof(rootParameters), rootParameters, _countof(staticSamplers), staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
		Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr; Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr; D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr; dxCommon->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

		D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = rootSignature.Get(); psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxCommon);
		Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxCommon);
		psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
		psoDesc.DepthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL }; psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.NumRenderTargets = 1; psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; psoDesc.SampleDesc.Count = 1; psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState = nullptr; dxCommon->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));

		D3D12_HEAP_PROPERTIES uploadHeapProperties{ D3D12_HEAP_TYPE_UPLOAD };

		ModelData planeModelData = LoadObjFile("resources", "plane.obj");
		ModelData sphereModelData = CreateSphereModel(32);

		// Planeバッファ
		D3D12_RESOURCE_DESC vertexResourceDescPlane{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * planeModelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourcePlane = nullptr;
		dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescPlane, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourcePlane));
		Vertex* vertexDataPlane = nullptr; vertexResourcePlane->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataPlane));
		std::memcpy(vertexDataPlane, planeModelData.vertices.data(), sizeof(Vertex) * planeModelData.vertices.size());
		vertexResourcePlane->Unmap(0, nullptr);
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewPlane{ vertexResourcePlane->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(Vertex) * planeModelData.vertices.size()), sizeof(Vertex) };

		// Sphereバッファ
		D3D12_RESOURCE_DESC vertexResourceDescSphere{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * sphereModelData.vertices.size(), 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere = nullptr;
		dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescSphere, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSphere));
		Vertex* vertexDataSphere = nullptr; vertexResourceSphere->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSphere));
		std::memcpy(vertexDataSphere, sphereModelData.vertices.data(), sizeof(Vertex) * sphereModelData.vertices.size());
		vertexResourceSphere->Unmap(0, nullptr);
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere{ vertexResourceSphere->GetGPUVirtualAddress(), static_cast<UINT>(sizeof(Vertex) * sphereModelData.vertices.size()), sizeof(Vertex) };

		// Sprite Vertex Buffer
		D3D12_RESOURCE_DESC vertexResourceDescSprite{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(Vertex) * 4, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexResourceSprite));
		D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{ vertexResourceSprite->GetGPUVirtualAddress(), sizeof(Vertex) * 4, sizeof(Vertex) };
		Vertex* vertexDataSprite = nullptr; vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
		Vector3 nSprite = { 0.0f, 0.0f, -1.0f };
		vertexDataSprite[0] = { {0.0f, 360.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, nSprite };
		vertexDataSprite[1] = { {0.0f, 0.0f, 0.0f, 1.0f},   {0.0f, 0.0f}, nSprite };
		vertexDataSprite[2] = { {640.0f, 360.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, nSprite };
		vertexDataSprite[3] = { {640.0f, 0.0f, 0.0f, 1.0f},   {1.0f, 0.0f}, nSprite };
		vertexResourceSprite->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC indexResourceDescSprite{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, sizeof(uint32_t) * 6, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &indexResourceDescSprite, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexResourceSprite));
		D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{ indexResourceSprite->GetGPUVirtualAddress(), sizeof(uint32_t) * 6, DXGI_FORMAT_R32_UINT };
		uint32_t* indexDataSprite = nullptr; indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
		indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2; indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;
		indexResourceSprite->Unmap(0, nullptr);

		D3D12_RESOURCE_DESC materialResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(Material) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResource));
		Material* materialData = nullptr; materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
		materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; materialData->enableLighting = 1; materialData->lightingType = 2; // デフォルト: Half Lambert
		materialData->uvTransform = Matrix4x4::MakeIdentity4x4();

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &materialResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialResourceSprite));
		Material* materialDataSprite = nullptr; materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
		materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f }; materialDataSprite->enableLighting = 0; materialDataSprite->lightingType = 0;
		materialDataSprite->uvTransform = Matrix4x4::MakeIdentity4x4();

		D3D12_RESOURCE_DESC wvpResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(TransformationMatrix) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&wvpResource));
		TransformationMatrix* wvpData = nullptr; wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
		wvpData->wvp = Matrix4x4::MakeIdentity4x4(); wvpData->world = Matrix4x4::MakeIdentity4x4();

		Transform transformModel;

		Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &wvpResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transformationMatrixResourceSprite));
		TransformationMatrix* transformationMatrixDataSprite = nullptr; transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
		transformationMatrixDataSprite->wvp = Matrix4x4::MakeIdentity4x4(); transformationMatrixDataSprite->world = Matrix4x4::MakeIdentity4x4();
		Transform transformSprite;
		Transform uvTransformSprite;

		D3D12_RESOURCE_DESC directionalLightResourceDesc{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, (sizeof(DirectionalLight) + 255) & ~255, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
		Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &directionalLightResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&directionalLightResource));
		DirectionalLight* directionalLightData = nullptr; directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
		directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f }; directionalLightData->direction = { 0.0f, -1.0f, 0.0f }; directionalLightData->intensity = 1.0f;

		// Textures
		std::vector<uint8_t> textureData; D3D12_RESOURCE_DESC textureDesc{}; LoadTextureFile(L"Resources/uvChecker.png", textureData, textureDesc);
		D3D12_HEAP_PROPERTIES textureHeapProps{ D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0 };
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource));
		textureResource->WriteToSubresource(0, nullptr, textureData.data(), (UINT)(textureDesc.Width * 4), (UINT)textureData.size());

		std::vector<uint8_t> textureData2; D3D12_RESOURCE_DESC textureDesc2{}; LoadTextureFile(L"Resources/monsterBall.png", textureData2, textureDesc2);
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource2 = nullptr; dxCommon->GetDevice()->CreateCommittedResource(&textureHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc2, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureResource2));
		textureResource2->WriteToSubresource(0, nullptr, textureData2.data(), (UINT)(textureDesc2.Width * 4), (UINT)textureData2.size());

		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU1 = dxCommon->GetSRVCPUDescriptorHandle(1);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = dxCommon->GetSRVGPUDescriptorHandle(1);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{ textureDesc.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc1.Texture2D.MipLevels = 1;
		dxCommon->GetDevice()->CreateShaderResourceView(textureResource.Get(), &srvDesc1, textureSrvHandleCPU1);

		D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = dxCommon->GetSRVCPUDescriptorHandle(2);
		D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = dxCommon->GetSRVGPUDescriptorHandle(2);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{ textureDesc2.Format, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING }; srvDesc2.Texture2D.MipLevels = 1;
		dxCommon->GetDevice()->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

		DebugCamera debugCamera; debugCamera.Initialize();
		bool isAutoRotate = false; bool useMonsterBall = false;
		float pi_f = 3.1415926535f;
		bool drawSprite = true;

		// 表示モデル切り替え (0: Plane, 1: Sphere)
		int currentModelType = 1;

		// ゲームループ
		while (true) {
			// Windowsのメッセージ処理
			if (winApp->ProcessMessage()) {
				break;
			}

			// --- 入力の更新 ---
			input->Update();

			// DebugCameraの更新
			debugCamera.Update(input->GetKeyBuffer());

			if (isAutoRotate) { transformModel.rotate.y += 0.01f; if (transformModel.rotate.y > pi_f * 2.0f) transformModel.rotate.y -= pi_f * 2.0f; }

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
			ImGui::Begin("Settings");

			// モデル選択
			const char* modelNames[] = { "Plane ", "Sphere " };
			ImGui::Combo("Model Select", &currentModelType, modelNames, IM_ARRAYSIZE(modelNames));

			if (ImGui::CollapsingHeader("Object (SRT)", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::DragFloat3("Translate", &transformModel.translate.x, 0.01f);
				ImGui::SliderFloat3("Rotate", &transformModel.rotate.x, -pi_f, pi_f);
				ImGui::DragFloat3("Scale", &transformModel.scale.x, 0.01f);
				ImGui::Checkbox("Auto Rotate", &isAutoRotate);
			}

			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("Color", &materialData->color.x);
				ImGui::Checkbox("useMonsterBall Texture", &useMonsterBall);

				// ライティングモード選択
				const char* lightingItems[] = { "None ", "Lambert", "Half Lambert" };
				int currentLighting = materialData->lightingType;
				if (ImGui::Combo("Lighting Mode", &currentLighting, lightingItems, IM_ARRAYSIZE(lightingItems))) {
					materialData->lightingType = currentLighting;
					materialData->enableLighting = (currentLighting > 0) ? 1 : 0;
				}
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

			if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat3("Light Direction", &directionalLightData->direction.x, -1.0f, 1.0f);
				ImGui::ColorEdit4("Light Color", &directionalLightData->color.x);
				ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 5.0f);
			}

			ImGui::End(); ImGui::Render();
#endif

			Matrix4x4 materialTransformMatrix = MakeUVTransformMatrix(uvTransformSprite.scale, uvTransformSprite.rotate.z, uvTransformSprite.translate);
			materialDataSprite->uvTransform = materialTransformMatrix;

			Matrix4x4 viewMatrix = debugCamera.GetMatView();
			Matrix4x4 worldMatrixModel = Matrix4x4::MakeAffineMatrix(transformModel.scale, transformModel.rotate, transformModel.translate);
			Matrix4x4 projectionMatrixModel = Matrix4x4::MakePerspectiveFovMatrix(0.45f, (float)kClientWidth / kClientHeight, 0.1f, 100.0f);
			wvpData->wvp = Matrix4x4::Multiply(Matrix4x4::Multiply(worldMatrixModel, viewMatrix), projectionMatrixModel);
			wvpData->world = worldMatrixModel;

			Matrix4x4 worldMatrixSprite = Matrix4x4::MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 projectionMatrixSprite = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, (float)kClientWidth, (float)kClientHeight, 0.0f, 100.0f);
			transformationMatrixDataSprite->wvp = Matrix4x4::Multiply(worldMatrixSprite, projectionMatrixSprite);
			transformationMatrixDataSprite->world = worldMatrixSprite;

			// 描画前処理
			dxCommon->PreDraw();
			ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(pipelineState.Get());
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

			// 3Dモデル描画
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU1);

			if (currentModelType == 0) {
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewPlane);
				commandList->DrawInstanced(static_cast<UINT>(planeModelData.vertices.size()), 1, 0, 0);
			} else {
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere);
				commandList->DrawInstanced(static_cast<UINT>(sphereModelData.vertices.size()), 1, 0, 0);
			}

			// 2Dスプライト描画
			if (drawSprite) {
				commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
				commandList->IASetIndexBuffer(&indexBufferViewSprite);
				commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			// 描画後処理
			dxCommon->PostDraw();
		}

#ifdef USE_IMGUI
		ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
#endif

		// --- 解放処理 ---
		delete input;
		input = nullptr;

		xAudio2.Reset(); SoundUnload(&soundData1);

		directionalLightResource->Unmap(0, nullptr); materialResourceSprite->Unmap(0, nullptr);
		transformationMatrixResourceSprite->Unmap(0, nullptr); wvpResource->Unmap(0, nullptr); materialResource->Unmap(0, nullptr);
	}

	// DirectXCommonの解放
	delete dxCommon;
	dxCommon = nullptr;

	// WinAppの終了処理の呼び出し
	winApp->Finalize();
	delete winApp;
	winApp = nullptr;

	return 0;
}