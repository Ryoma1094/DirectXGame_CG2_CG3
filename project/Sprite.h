#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"

// 前方宣言
class SpriteCommon;

// スプライト
class Sprite
{
public: // インナークラス構造体
	// 頂点データ
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	// マテリアルデータ
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};

	// 座標変換行列データ
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

public: // メンバ関数
	// 初期化
	void Initialize(SpriteCommon* spriteCommon);
	// 更新
	void Update();
	// 描画
	void Draw();

private: // メンバ関数
	// 頂点データ作成
	void CreateVertexData();
	// マテリアルデータ作成
	void CreateMaterialData();
	// 座標変換行列データ作成
	void CreateTransformationMatrixData();

private: // メンバ変数
	// スプライト共通部（借りてくるポインタなのでdeleteしない）
	SpriteCommon* spriteCommon_ = nullptr;

	// バッファリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_ = nullptr;

	// バッファリソース内のデータを指すポインタ
	VertexData* vertexData_ = nullptr;
	uint32_t* indexData_ = nullptr;
	Material* materialData_ = nullptr;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	// バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	// SRV（テクスチャ）のGPUハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_{};

public: // テクスチャ設定用
	void SetTextureSrvHandleGPU(D3D12_GPU_DESCRIPTOR_HANDLE handle) { textureSrvHandleGPU_ = handle; }
};