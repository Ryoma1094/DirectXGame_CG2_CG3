#include "Sprite.h"
#include "SpriteCommon.h"
#include "WinApp.h"
#include "TextureManager.h"

void Sprite::Initialize(SpriteCommon* spriteCommon, std::string textureFilePath)
{
	// 引数で受け取ってメンバ変数に記録する
	this->spriteCommon_ = spriteCommon;

	// 頂点データ作成
	CreateVertexData();

	// マテリアルデータ作成
	CreateMaterialData();

	// 座標変換行列データ作成
	CreateTransformationMatrixData();

	// テクスチャ番号を検索して記録
	textureIndex_ = TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath);

	// テクスチャサイズをイメージに合わせる
	AdjustTextureSize();
}

void Sprite::Update()
{
	// アンカーポイントを反映した左右上下の座標計算
	float left = 0.0f - anchorPoint_.x;
	float right = 1.0f - anchorPoint_.x;
	float top = 0.0f - anchorPoint_.y;
	float bottom = 1.0f - anchorPoint_.y;

	// 左右反転
	if (isFlipX_) {
		left = -left;
		right = -right;
	}
	// 上下反転
	if (isFlipY_) {
		top = -top;
		bottom = -bottom;
	}

	// テクスチャ範囲指定のUV計算
	const DirectX::TexMetadata& metadata =
		TextureManager::GetInstance()->GetMetaData(textureIndex_);
	float tex_left = textureLeftTop_.x / metadata.width;
	float tex_right = (textureLeftTop_.x + textureSize_.x) / metadata.width;
	float tex_top = textureLeftTop_.y / metadata.height;
	float tex_bottom = (textureLeftTop_.y + textureSize_.y) / metadata.height;

	// 頂点リソースにデータを書き込む
	// 左下
	vertexData_[0].position = { left, bottom, 0.0f, 1.0f };
	vertexData_[0].texcoord = { tex_left, tex_bottom };
	vertexData_[0].normal = { 0.0f, 0.0f, -1.0f };
	// 左上
	vertexData_[1].position = { left, top, 0.0f, 1.0f };
	vertexData_[1].texcoord = { tex_left, tex_top };
	vertexData_[1].normal = { 0.0f, 0.0f, -1.0f };
	// 右下
	vertexData_[2].position = { right, bottom, 0.0f, 1.0f };
	vertexData_[2].texcoord = { tex_right, tex_bottom };
	vertexData_[2].normal = { 0.0f, 0.0f, -1.0f };
	// 右上
	vertexData_[3].position = { right, top, 0.0f, 1.0f };
	vertexData_[3].texcoord = { tex_right, tex_top };
	vertexData_[3].normal = { 0.0f, 0.0f, -1.0f };

	// インデックスリソースにデータを書き込む（6個分）
	indexData_[0] = 0; indexData_[1] = 1; indexData_[2] = 2;
	indexData_[3] = 1; indexData_[4] = 3; indexData_[5] = 2;

	// Transform情報を作り、メンバ変数の値を反映する
	Transform transform{};
	transform.scale = { size_.x, size_.y, 1.0f };
	transform.rotate = { 0.0f, 0.0f, rotation_ };
	transform.translate = { position_.x, position_.y, 0.0f };

	// TransformからWorldMatrixを作る
	Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	// ViewMatrixを作って単位行列を代入
	Matrix4x4 viewMatrix = Matrix4x4::MakeIdentity4x4();
	// ProjectionMatrixを作って平行投影行列を書き込む
	Matrix4x4 projectionMatrix = Matrix4x4::MakeOrthographicMatrix(
		0.0f, 0.0f, static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight), 0.0f, 100.0f);

	transformationMatrixData_->WVP = Matrix4x4::Multiply(worldMatrix, Matrix4x4::Multiply(viewMatrix, projectionMatrix));
	transformationMatrixData_->World = worldMatrix;
}

void Sprite::Draw()
{
	ID3D12GraphicsCommandList* commandList = spriteCommon_->GetDxCommon()->GetCommandList();

	// VertexBufferViewを設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	// IndexBufferViewを設定
	commandList->IASetIndexBuffer(&indexBufferView_);

	// マテリアルCBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	// 座標変換行列CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

	// SRVのDescriptorTableの先頭を設定
	commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(textureIndex_));

	// 描画（DrawCall/ドローコール）
	commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::CreateVertexData()
{
	DirectXCommon* dxCommon = spriteCommon_->GetDxCommon();

	// VertexResourceを作る
	vertexResource_ = dxCommon->CreateBufferResource(sizeof(VertexData) * 4);
	// IndexResourceを作る
	indexResource_ = dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);

	// VertexBufferViewを作成する（値を設定するだけ）
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = sizeof(VertexData) * 4;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// IndexBufferViewを作成する（値を設定するだけ）
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	// VertexResourceにデータを書き込むためのアドレスを取得してvertexDataに割り当てる
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
	// IndexResourceにデータを書き込むためのアドレスを取得してindexDataに割り当てる
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData_));
}

void Sprite::CreateMaterialData()
{
	DirectXCommon* dxCommon = spriteCommon_->GetDxCommon();

	// マテリアルリソースを作る
	materialResource_ = dxCommon->CreateBufferResource((sizeof(Material) + 255) & ~255);

	// マテリアルリソースにデータを書き込むためのアドレスを取得してmaterialDataに割り当てる
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

	// マテリアルデータの初期値を書き込む
	materialData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->enableLighting = false;
	materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();
}

void Sprite::CreateTransformationMatrixData()
{
	DirectXCommon* dxCommon = spriteCommon_->GetDxCommon();

	// 座標変換行列リソースを作る
	transformationMatrixResource_ = dxCommon->CreateBufferResource((sizeof(TransformationMatrix) + 255) & ~255);

	// 座標変換行列リソースにデータを書き込むためのアドレスを取得してtransformationMatrixDataに割り当てる
	transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

	// 単位行列を書き込んでおく
	transformationMatrixData_->WVP = Matrix4x4::MakeIdentity4x4();
	transformationMatrixData_->World = Matrix4x4::MakeIdentity4x4();
}

void Sprite::AdjustTextureSize()
{
	// テクスチャメタデータを取得
	const DirectX::TexMetadata& metadata = TextureManager::GetInstance()->GetMetaData(textureIndex_);

	textureSize_.x = static_cast<float>(metadata.width);
	textureSize_.y = static_cast<float>(metadata.height);
	// 画像サイズをテクスチャサイズに合わせる
	size_ = textureSize_;
}