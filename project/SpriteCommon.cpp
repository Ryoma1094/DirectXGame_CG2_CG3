#include "SpriteCommon.h"
#include <cassert>

void SpriteCommon::Initialize(DirectXCommon* dxCommon)
{
	// 引数で受け取ってメンバ変数に記録する
	dxCommon_ = dxCommon;

	// グラフィックスパイプラインの生成（内部でルートシグネチャも作成される）
	CreateGraphicsPipeline();
}

void SpriteCommon::CommonDraw()
{
	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

	// ルートシグネチャをセットするコマンド
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	// グラフィックスパイプラインステートをセットするコマンド
	commandList->SetPipelineState(graphicsPipelineState_.Get());
	// プリミティブトポロジーをセットするコマンド
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void SpriteCommon::CreateRootSignature()
{
	HRESULT hr = S_FALSE;

	// RootParameter作成
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL };
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_VERTEX };
	rootParameters[1].Descriptor.ShaderRegister = 1;

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {
		{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND}
	};
	rootParameters[2] = { D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL };
	rootParameters[2].DescriptorTable = { 1, descriptorRange };

	rootParameters[3] = { D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL };
	rootParameters[3].Descriptor.ShaderRegister = 1;

	// Sampler作成
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {
		{
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			0, 0,
			D3D12_COMPARISON_FUNC_NEVER,
			D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
			0.0f, D3D12_FLOAT32_MAX,
			0, 0,
			D3D12_SHADER_VISIBILITY_PIXEL
		}
	};

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{
		_countof(rootParameters),
		rootParameters,
		_countof(staticSamplers),
		staticSamplers,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	};

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	assert(SUCCEEDED(hr));

	hr = dxCommon_->GetDevice()->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void SpriteCommon::CreateGraphicsPipeline()
{
	// ルートシグネチャの作成を最初に呼び出す
	CreateRootSignature();

	HRESULT hr = S_FALSE;

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature_.Get();
	psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

	// Shaderのコンパイル
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon_->CompileShader(L"resources/HLSL/Object3d.VS.hlsl", L"vs_6_0");
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon_->CompileShader(L"resources/HLSL/Object3d.PS.hlsl", L"ps_6_0");
	psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
	psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };

	// BlendState / RasterizerState / DepthStencilState
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.RasterizerState = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE };
	psoDesc.DepthStencilState = { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL };
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineState_));
	assert(SUCCEEDED(hr));
}