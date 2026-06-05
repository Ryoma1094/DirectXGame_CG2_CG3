#pragma once
#include <cmath>

class Vector3 {
public:
	float x;
	float y;
	float z;

public:
	// コンストラクタ
	Vector3();
	Vector3(float x, float y, float z);

	// 加算
	static Vector3 Add(const Vector3& v1, const Vector3& v2);

	// 減算
	static Vector3 Subtract(const Vector3& v1, const Vector3& v2);

	// スカラー倍
	static Vector3 Multiply(float scalar, const Vector3& v);

	// 内積
	static float Dot(const Vector3& v1, const Vector3& v2);

	// 長さ
	static float Length(const Vector3& v);

	// 正規化
	static Vector3 Normalize(const Vector3& v);

	// クロス積
	static Vector3 Cross(const Vector3& v1, const Vector3& v2);
};