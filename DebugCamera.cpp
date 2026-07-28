#include "DebugCamera.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

/// <summary>
/// 初期化
/// </summary>
void DebugCamera::Initialize()
{
    // 各種メンバ変数の初期化（回転行列も単位行列で初期化）
    matRot_ = Matrix4x4::MakeIdentity4x4();
    matView_ = Matrix4x4::MakeIdentity4x4();
    matProjection_ = Matrix4x4::MakeIdentity4x4();
}

/// <summary>
/// 更新
/// </summary>
void DebugCamera::Update(const uint8_t* keys)
{
    if (!keys) return;

    // --------------------------------------------------
    // 移動・回転スピードの設定
    // --------------------------------------------------
    const float kMoveSpeed = 0.5f;     // 移動の速さ
    const float kRotateSpeed = 0.02f;  // 回転の速さ（ラジアン）

    // ==================================================
    // 1. 入力によるカメラの移動や回転 (DirectInputのバッファ参照)
    // ==================================================

    // --------------------------------------------------
    // 【回転処理】
    // --------------------------------------------------
    float rotX = 0.0f; // 今回のX軸回転角度
    float rotY = 0.0f; // 今回のY軸回転角度

    // X軸周りの回転（上下）
    if (keys[DIK_UP] & 0x80) { rotX -= kRotateSpeed; }
    if (keys[DIK_DOWN] & 0x80) { rotX += kRotateSpeed; }

    // Y軸周りの回転（左右）
    if (keys[DIK_LEFT] & 0x80) { rotY -= kRotateSpeed; }
    if (keys[DIK_RIGHT] & 0x80) { rotY += kRotateSpeed; }

    // 追加回転分の回転行列を生成
    Matrix4x4 matRotDelta = Matrix4x4::MakeIdentity4x4();
    matRotDelta = Matrix4x4::Multiply(matRotDelta, Matrix4x4::MakeRotateXMatrix(rotX));
    matRotDelta = Matrix4x4::Multiply(matRotDelta, Matrix4x4::MakeRotateYMatrix(rotY));

    // 累積の回転行列を合成 ( matRot_ = matRotDelta * matRot_ )
    matRot_ = Matrix4x4::Multiply(matRotDelta, matRot_);


    // --------------------------------------------------
    // 【前後左右移動】 (累積回転行列 matRot_ を適用)
    // --------------------------------------------------
    // 前後移動 (W/S)
    if (keys[DIK_W] & 0x80)
    {
        const float speed = kMoveSpeed;
        Vector3 move = { 0.0f, 0.0f, speed };
        move = Matrix4x4::Transform(move, matRot_);
        translation_ = Vector3::Add(translation_, move);
    }
    if (keys[DIK_S] & 0x80)
    {
        const float speed = -kMoveSpeed;
        Vector3 move = { 0.0f, 0.0f, speed };
        move = Matrix4x4::Transform(move, matRot_);
        translation_ = Vector3::Add(translation_, move);
    }

    // 左右移動 (A/D)
    if (keys[DIK_D] & 0x80)
    {
        const float speed = kMoveSpeed;
        Vector3 move = { speed, 0.0f, 0.0f };
        move = Matrix4x4::Transform(move, matRot_);
        translation_ = Vector3::Add(translation_, move);
    }
    if (keys[DIK_A] & 0x80)
    {
        const float speed = -kMoveSpeed;
        Vector3 move = { speed, 0.0f, 0.0f };
        move = Matrix4x4::Transform(move, matRot_);
        translation_ = Vector3::Add(translation_, move);
    }


    // ==================================================
    // 2. ビュー行列の更新
    // ==================================================

    // 1. 座標から平行移動行列を計算する
    Matrix4x4 matTranslate = Matrix4x4::MakeTranslateMatrix(translation_);

    // 2. 累積回転行列と平行移動行列からワールド行列を計算する
    Matrix4x4 matWorld = Matrix4x4::Multiply(matRot_, matTranslate);

    // 3. ワールド行列の逆行列をビュー行列に代入する
    matView_ = Matrix4x4::Inverse(matWorld);
}