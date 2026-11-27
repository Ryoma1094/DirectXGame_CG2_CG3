#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include <string>

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
	void Initialize(SpriteCommon* spriteCommon, std::string textureFilePath);
	// 更新
	void Update();
	// 描画
	void Draw();

public: // getter / setter
	// 座標
	const Vector2& GetPosition() const { return position_; }
	void SetPosition(const Vector2& position) { this->position_ = position; }

	// 回転角
	float GetRotation() const { return rotation_; }
	void SetRotation(float rotation) { this->rotation_ = rotation; }

	// 色
	const Vector4& GetColor() const { return materialData_->color; }
	void SetColor(const Vector4& color) { materialData_->color = color; }

	// 表示サイズ
	const Vector2& GetSize() const { return size_; }
	void SetSize(const Vector2& size) { this->size_ = size; }

	// アンカーポイント
	const Vector2& GetAnchorPoint() const { return anchorPoint_; }
	void SetAnchorPoint(const Vector2& anchorPoint) { this->anchorPoint_ = anchorPoint; }

	// 左右フリップ
	bool GetIsFlipX() const { return isFlipX_; }
	void SetIsFlipX(bool isFlipX) { this->isFlipX_ = isFlipX; }

	// 上下フリップ
	bool GetIsFlipY() const { return isFlipY_; }
	void SetIsFlipY(bool isFlipY) { this->isFlipY_ = isFlipY; }

	// テクスチャ左上座標
	const Vector2& GetTextureLeftTop() const { return textureLeftTop_; }
	void SetTextureLeftTop(const Vector2& textureLeftTop) { this->textureLeftTop_ = textureLeftTop; }

	// テクスチャ切り出しサイズ
	const Vector2& GetTextureSize() const { return textureSize_; }
	void SetTextureSize(const Vector2& textureSize) { this->textureSize_ = textureSize; }

private: // メンバ関数
	// 頂点データ作成
	void CreateVertexData();
	// マテリアルデータ作成
	void CreateMaterialData();
	// 座標変換行列データ作成
	void CreateTransformationMatrixData();
	// テクスチャサイズをイメージに合わせる
	void AdjustTextureSize();

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

	// パラメータ（座標・回転角・表示サイズ）
	Vector2 position_ = { 0.0f, 0.0f };
	float rotation_ = 0.0f;
	Vector2 size_ = { 640.0f, 360.0f };

	// アンカーポイント
	Vector2 anchorPoint_ = { 0.0f, 0.0f };

	// 左右フリップ
	bool isFlipX_ = false;
	// 上下フリップ
	bool isFlipY_ = false;

	// テクスチャ左上座標
	Vector2 textureLeftTop_ = { 0.0f, 0.0f };
	// テクスチャ切り出しサイズ
	Vector2 textureSize_ = { 100.0f, 100.0f };

	// テクスチャ番号
	uint32_t textureIndex_ = 0;
};