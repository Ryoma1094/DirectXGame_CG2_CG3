#include "Vector3.h"

// コンストラクタ
Vector3::Vector3() {
	x = 0.0f;
	y = 0.0f;
	z = 0.0f;
}

Vector3::Vector3(float x, float y, float z) {
	this->x = x;
	this->y = y;
	this->z = z;
}

// 加算
Vector3 Vector3::Add(const Vector3& v1, const Vector3& v2) {
	return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

// 減算
Vector3 Vector3::Subtract(const Vector3& v1, const Vector3& v2) {
	return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

// スカラー倍
Vector3 Vector3::Multiply(float scalar, const Vector3& v) {
	return { v.x * scalar, v.y * scalar, v.z * scalar };
}

// 内積
float Vector3::Dot(const Vector3& v1, const Vector3& v2) {
	return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

// 長さ
float Vector3::Length(const Vector3& v) {
	return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// 正規化
Vector3 Vector3::Normalize(const Vector3& v) {
	float length = Length(v);

	if (length == 0.0f) {
		return { 0, 0, 0 };
	}

	return { v.x / length, v.y / length, v.z / length };
}

// クロス積
Vector3 Vector3::Cross(const Vector3& v1, const Vector3& v2) {
	return {
		v1.y * v2.z - v1.z * v2.y,
		v1.z * v2.x - v1.x * v2.z,
		v1.x * v2.y - v1.y * v2.x
	};
}