#pragma once
#include <d3d12.h>
#include <wrl.h>
#include "DirectXCommon.h"

// スプライト共通部
class SpriteCommon
{
public: // メンバ関数
	// 初期化
	void Initialize(DirectXCommon* dxCommon);

	// 共通描画設定
	void CommonDraw();

	// getter
	DirectXCommon* GetDxCommon() const { return dxCommon_; }

private: // メンバ関数
	// ルートシグネチャの作成
	void CreateRootSignature();
	// グラフィックスパイプラインの生成
	void CreateGraphicsPipeline();

private: // メンバ変数
	// DirectXCommon（借りてくるポインタなのでdeleteしない）
	DirectXCommon* dxCommon_ = nullptr;

	// ルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
	// グラフィックスパイプラインステート
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;
};