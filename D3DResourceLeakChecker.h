#pragma once

/// <summary>
/// DirectX12 リソースリークチェッカー
/// </summary>
class D3DResourceLeakChecker
{
public:
	/// <summary>
	/// デストラクタ（終了時にリークを出力）
	/// </summary>
	~D3DResourceLeakChecker();
};