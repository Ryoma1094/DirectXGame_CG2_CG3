#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"
#include <cstdint>

/// <summary>
/// デバッグカメラ
/// </summary>
class DebugCamera
{
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新 (DirectInputのキー入力バッファを受け取る)
    /// </summary>
    /// <param name="keys">キー入力状態配列</param>
    void Update(const uint8_t* keys);

    // ゲッター関数
    const Matrix4x4& GetMatView() const { return matView_; }
    const Matrix4x4& GetMatProjection() const { return matProjection_; }

private:
    // 累積回転行列
    Matrix4x4 matRot_{};

    // ローカル座標
    Vector3 translation_ = { 0.0f, 0.0f, -50.0f };

    // ビュー行列
    Matrix4x4 matView_{};

    // 射影行列
    Matrix4x4 matProjection_{};
};