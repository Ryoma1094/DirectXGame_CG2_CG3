#pragma once
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <string>
#include <dxcapi.h>
#include "WinApp.h"
#include "externals/DirectXTex/DirectXTex.h"

class DirectXCommon
{
public:
	// 初期化
	void Initialize(WinApp* winApp);

	// デスクリプタヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

	// SRV専用のデスクリプタハンドル取得関数
	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

	// 描画・リソース作成等で利用するゲッター群
	ID3D12Device* GetDevice() const { return device.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList.Get(); }
	ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator.Get(); }
	IDXGISwapChain4* GetSwapChain() const { return swapChain.Get(); }
	ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap.Get(); }
	ID3D12DescriptorHeap* GetRtvDescriptorHeap() const { return rtvDescriptorHeap.Get(); }
	ID3D12DescriptorHeap* GetDsvDescriptorHeap() const { return dsvDescriptorHeap.Get(); }
	uint32_t GetDescriptorSizeSRV() const { return descriptorSizeSRV; }
	uint32_t GetDescriptorSizeRTV() const { return descriptorSizeRTV; }
	uint32_t GetDescriptorSizeDSV() const { return descriptorSizeDSV; }
	const D3D12_VIEWPORT& GetViewport() const { return viewport; }
	const D3D12_RECT& GetScissorRect() const { return scissorRect; }
	IDxcUtils* GetDxcUtils() const { return dxcUtils.Get(); }
	IDxcCompiler3* GetDxcCompiler() const { return dxcCompiler.Get(); }
	IDxcIncludeHandler* GetIncludeHandler() const { return includeHandler.Get(); }
	const D3D12_CPU_DESCRIPTOR_HANDLE& GetRtvHandle(uint32_t index) const { return rtvHandles[index]; }
	ID3D12Resource* GetSwapChainResource(uint32_t index) const { return swapChainResources[index].Get(); }
	ID3D12Fence* GetFence() const { return fence.Get(); }
	UINT64 GetFenceVal() const { return fenceVal; }
	HANDLE GetFenceEvent() const { return fenceEvent; }

	// シェーダーコンパイル関数
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

	// バッファリソース生成関数
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	// テクスチャリソース生成関数
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);

	// テクスチャデータ転送関数
	void UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages);

	// テクスチャファイル読み込み関数 (static)
	static DirectX::ScratchImage LoadTexture(const std::string& filePath);

	// コマンド実行・画面同期制御
	void PreDraw();
	void PostDraw();

private:
	// 内部用：デスクリプタハンドル取得関数
	static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index);
	static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index);

	// 小分けにした初期化関数群
	void InitializeDevice();
	void InitializeCommand();
	void CreateSwapChain();
	void CreateDepthBuffer();
	void CreateDescriptorHeaps();
	void InitializeRenderTargetView();
	void InitializeDepthStencilView();
	void InitializeFence();
	void InitializeViewport();
	void InitializeScissorRect();
	void CreateDXCCompiler();
	void InitializeImGui();

private:
	WinApp* winApp = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Device> device;
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> swapChainResources;

	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;

	uint32_t descriptorSizeRTV = 0;
	uint32_t descriptorSizeSRV = 0;
	uint32_t descriptorSizeDSV = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]{};

	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	UINT64 fenceVal = 0;
	HANDLE fenceEvent = nullptr;

	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;
};