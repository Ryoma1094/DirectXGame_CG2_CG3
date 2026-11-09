#include "DirectXCommon.h"
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

#ifndef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

using namespace Microsoft::WRL;

void DirectXCommon::Initialize(WinApp* winApp)
{
	// NULL検出
	assert(winApp);
	// メンバ変数に記録
	this->winApp = winApp;

	// 初期化処理の順次呼び出し
	InitializeDevice();
	InitializeCommand();
	CreateSwapChain();
	CreateDepthBuffer();
	CreateDescriptorHeaps();
	InitializeRenderTargetView();
	InitializeDepthStencilView();
	InitializeFence();
	InitializeViewport();
	InitializeScissorRect();
	CreateDXCCompiler();
	InitializeImGui();
}

void DirectXCommon::InitializeDevice() {
#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));

	ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	hr = dxgiFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter));
	assert(SUCCEEDED(hr));

	hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
	assert(SUCCEEDED(hr));

#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		infoQueue->SetBreakOnSeverity(D3D12_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_SEVERITY_WARNING, true);
	}
#endif
}

void DirectXCommon::InitializeCommand() {
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HRESULT hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	assert(SUCCEEDED(hr));

	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	assert(SUCCEEDED(hr));

	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateSwapChain() {
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = WinApp::kClientWidth;
	swapChainDesc.Height = WinApp::kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.Get(),
		winApp->GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf())
	);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateDepthBuffer() {
	D3D12_RESOURCE_DESC resourceDesc{
		D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0,
		(UINT64)WinApp::kClientWidth, (UINT)WinApp::kClientHeight,
		1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1, 0},
		D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	};
	D3D12_HEAP_PROPERTIES heapProperties{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_CLEAR_VALUE depthClearValue{ DXGI_FORMAT_D24_UNORM_S8_UINT, {1.0f, 0} };

	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&depthStencilResource)
	);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::CreateDescriptorHeaps() {
	descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	srvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
}

void DirectXCommon::InitializeRenderTargetView() {
	swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D12_RTV_DIMENSION_TEXTURE2D };
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 0);
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 1);

	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);
}

void DirectXCommon::InitializeDepthStencilView() {
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{ DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D };
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0));
}

void DirectXCommon::InitializeFence() {
	HRESULT hr = device->CreateFence(fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));
	fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);
}

void DirectXCommon::InitializeViewport() {
	viewport = D3D12_VIEWPORT{ 0.0f, 0.0f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.0f, 1.0f };
}

void DirectXCommon::InitializeScissorRect() {
	scissorRect = D3D12_RECT{ 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
}

void DirectXCommon::CreateDXCCompiler() {
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeImGui() {
#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(
		device.Get(), 2, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		srvDescriptorHeap.Get(),
		GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0)
	);
#endif
}

ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{
		heapType,
		numDescriptors,
		shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		0
	};
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(const ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSRVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetSRVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
}

// 描画前処理
void DirectXCommon::PreDraw() {
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

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0);
	commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
}

// 描画後処理
void DirectXCommon::PostDraw() {
	// バックバッファの番号取得
	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList->ResourceBarrier(1, &barrier);

	commandList->Close();
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);

	swapChain->Present(1, 0);

	// メンバ変数fenceValを使用
	fenceVal++;
	commandQueue->Signal(fence.Get(), fenceVal);
	if (fence->GetCompletedValue() < fenceVal) {
		fence->SetEventOnCompletion(fenceVal, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);
}