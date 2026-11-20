#pragma once
#include "Vector3.h"
class Matrix4x4 {
public:
	float m[4][4];

public:
	// 単位行列
	static Matrix4x4 MakeIdentity4x4();

	// 行列の加法
	static Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);

	// 行列の減法
	static Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);

	// 行列の積
	static Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);

	// 転置行列
	static Matrix4x4 Transpose(const Matrix4x4& m);

	// 逆行列
	static Matrix4x4 Inverse(const Matrix4x4& m);

	// 拡大縮小行列
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);

	// 平行移動行列
	static Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

	// X軸回転行列
	static Matrix4x4 MakeRotateXMatrix(float radian);

	// Y軸回転行列
	static Matrix4x4 MakeRotateYMatrix(float radian);

	// Z軸回転行列
	static Matrix4x4 MakeRotateZMatrix(float radian);

	// XYZ回転まとめ
	static Matrix4x4 MakeRotateMatrix(const Vector3& rotate);

	// アフィン変換行列
	static Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

	// 透視投影行列
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	// 正射影行列
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	// ビューポート変換行列
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

	// 座標変換
	static Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix);
};