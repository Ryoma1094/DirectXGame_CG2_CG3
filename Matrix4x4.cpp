#include "Matrix4x4.h"
#include "Vector3.h"
#include <cmath>

// 単位行列
Matrix4x4 Matrix4x4::MakeIdentity4x4() {
	Matrix4x4 result{};

	for (int i = 0; i < 4; i++) {
		result.m[i][i] = 1.0f;
	}

	return result;
}

// 加算
Matrix4x4 Matrix4x4::Add(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] + m2.m[i][j];
		}
	}

	return result;
}

// 減算
Matrix4x4 Matrix4x4::Subtract(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] - m2.m[i][j];
		}
	}

	return result;
}

// 乗算
Matrix4x4 Matrix4x4::Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result{};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] +
				m1.m[i][1] * m2.m[1][j] +
				m1.m[i][2] * m2.m[2][j] +
				m1.m[i][3] * m2.m[3][j];
		}
	}
	return result;
}

// 転置
Matrix4x4 Matrix4x4::Transpose(const Matrix4x4& m) {
	Matrix4x4 result{};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m.m[j][i];
		}
	}

	return result;
}

// 逆行列
Matrix4x4 Matrix4x4::Inverse(const Matrix4x4& m) {

	Matrix4x4 result{};

	float inv[16];
	float mat[16] = { m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3], m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3], m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3], m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3] };

	inv[0] = mat[5] * mat[10] * mat[15] - mat[5] * mat[11] * mat[14] - mat[9] * mat[6] * mat[15] + mat[9] * mat[7] * mat[14] + mat[13] * mat[6] * mat[11] - mat[13] * mat[7] * mat[10];

	inv[1] = -mat[1] * mat[10] * mat[15] + mat[1] * mat[11] * mat[14] + mat[9] * mat[2] * mat[15] - mat[9] * mat[3] * mat[14] - mat[13] * mat[2] * mat[11] + mat[13] * mat[3] * mat[10];

	inv[2] = mat[1] * mat[6] * mat[15] - mat[1] * mat[7] * mat[14] - mat[5] * mat[2] * mat[15] + mat[5] * mat[3] * mat[14] + mat[13] * mat[2] * mat[7] - mat[13] * mat[3] * mat[6];

	inv[3] = -mat[1] * mat[6] * mat[11] + mat[1] * mat[7] * mat[10] + mat[5] * mat[2] * mat[11] - mat[5] * mat[3] * mat[10] - mat[9] * mat[2] * mat[7] + mat[9] * mat[3] * mat[6];

	inv[4] = -mat[4] * mat[10] * mat[15] + mat[4] * mat[11] * mat[14] + mat[8] * mat[6] * mat[15] - mat[8] * mat[7] * mat[14] - mat[12] * mat[6] * mat[11] + mat[12] * mat[7] * mat[10];

	inv[5] = mat[0] * mat[10] * mat[15] - mat[0] * mat[11] * mat[14] - mat[8] * mat[2] * mat[15] + mat[8] * mat[3] * mat[14] + mat[12] * mat[2] * mat[11] - mat[12] * mat[3] * mat[10];

	inv[6] = -mat[0] * mat[6] * mat[15] + mat[0] * mat[7] * mat[14] + mat[4] * mat[2] * mat[15] - mat[4] * mat[3] * mat[14] - mat[12] * mat[2] * mat[7] + mat[12] * mat[3] * mat[6];

	inv[7] = mat[0] * mat[6] * mat[11] - mat[0] * mat[7] * mat[10] - mat[4] * mat[2] * mat[11] + mat[4] * mat[3] * mat[10] + mat[8] * mat[2] * mat[7] - mat[8] * mat[3] * mat[6];

	inv[8] = mat[4] * mat[9] * mat[15] - mat[4] * mat[11] * mat[13] - mat[8] * mat[5] * mat[15] + mat[8] * mat[7] * mat[13] + mat[12] * mat[5] * mat[11] - mat[12] * mat[7] * mat[9];

	inv[9] = -mat[0] * mat[9] * mat[15] + mat[0] * mat[11] * mat[13] + mat[8] * mat[1] * mat[15] - mat[8] * mat[3] * mat[13] - mat[12] * mat[1] * mat[11] + mat[12] * mat[3] * mat[9];

	inv[10] = mat[0] * mat[5] * mat[15] - mat[0] * mat[7] * mat[13] - mat[4] * mat[1] * mat[15] + mat[4] * mat[3] * mat[13] + mat[12] * mat[1] * mat[7] - mat[12] * mat[3] * mat[5];

	inv[11] = -mat[0] * mat[5] * mat[11] + mat[0] * mat[7] * mat[9] + mat[4] * mat[1] * mat[11] - mat[4] * mat[3] * mat[9] - mat[8] * mat[1] * mat[7] + mat[8] * mat[3] * mat[5];

	inv[12] = -mat[4] * mat[9] * mat[14] + mat[4] * mat[10] * mat[13] + mat[8] * mat[5] * mat[14] - mat[8] * mat[6] * mat[13] - mat[12] * mat[5] * mat[10] + mat[12] * mat[6] * mat[9];

	inv[13] = mat[0] * mat[9] * mat[14] - mat[0] * mat[10] * mat[13] - mat[8] * mat[1] * mat[14] + mat[8] * mat[2] * mat[13] + mat[12] * mat[1] * mat[10] - mat[12] * mat[2] * mat[9];

	inv[14] = -mat[0] * mat[5] * mat[14] + mat[0] * mat[6] * mat[13] + mat[4] * mat[1] * mat[14] - mat[4] * mat[2] * mat[13] - mat[12] * mat[1] * mat[6] + mat[12] * mat[2] * mat[5];

	inv[15] = mat[0] * mat[5] * mat[10] - mat[0] * mat[6] * mat[9] - mat[4] * mat[1] * mat[10] + mat[4] * mat[2] * mat[9] + mat[8] * mat[1] * mat[6] - mat[8] * mat[2] * mat[5];

	float det = mat[0] * inv[0] + mat[1] * inv[4] + mat[2] * inv[8] + mat[3] * inv[12];

	if (det == 0.0f) {
		return MakeIdentity4x4();
	}

	det = 1.0f / det;

	for (int i = 0; i < 16; i++) {
		inv[i] *= det;
	}

	for (int row = 0; row < 4; row++) {
		for (int col = 0; col < 4; col++) {
			result.m[row][col] = inv[row * 4 + col];
		}
	}

	return result;
}

// 拡大縮小行列
Matrix4x4 Matrix4x4::MakeScaleMatrix(const Vector3& scale) {

	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;

	return result;
}

// 平行移動行列
Matrix4x4 Matrix4x4::MakeTranslateMatrix(const Vector3& translate) {

	Matrix4x4 result = MakeIdentity4x4();

	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;

	return result;
}

// X軸回転
Matrix4x4 Matrix4x4::MakeRotateXMatrix(float radian) {

	Matrix4x4 result = MakeIdentity4x4();

	result.m[1][1] = cosf(radian);
	result.m[1][2] = sinf(radian);

	result.m[2][1] = -sinf(radian);
	result.m[2][2] = cosf(radian);

	return result;
}

// Y軸回転
Matrix4x4 Matrix4x4::MakeRotateYMatrix(float radian) {

	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = cosf(radian);
	result.m[0][2] = -sinf(radian);

	result.m[2][0] = sinf(radian);
	result.m[2][2] = cosf(radian);

	return result;
}

// Z軸回転
Matrix4x4 Matrix4x4::MakeRotateZMatrix(float radian) {

	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = cosf(radian);
	result.m[0][1] = sinf(radian);

	result.m[1][0] = -sinf(radian);
	result.m[1][1] = cosf(radian);

	return result;
}

// XYZ回転まとめ
Matrix4x4 Matrix4x4::MakeRotateMatrix(const Vector3& rotate) {

	Matrix4x4 rotateX = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateY = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZ = MakeRotateZMatrix(rotate.z);

	// X → Y → Z の順
	Matrix4x4 result = Multiply(Multiply(rotateX, rotateY), rotateZ);

	return result;
}

// アフィン変換行列
Matrix4x4 Matrix4x4::MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateMatrix = MakeRotateMatrix(rotate);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	Matrix4x4 result = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

	return result;
}

// 透視投影行列
Matrix4x4 Matrix4x4::MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {

	Matrix4x4 result{};

	float f = 1.0f / tanf(fovY / 2.0f);

	result.m[0][0] = f / aspectRatio;
	result.m[1][1] = f;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);

	return result;
}

// 正射影行列
Matrix4x4 Matrix4x4::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {

	Matrix4x4 result{};

	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;

	return result;
}

// ビューポート変換行列
Matrix4x4 Matrix4x4::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {

	Matrix4x4 result{};

	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -(height / 2.0f);
	result.m[2][2] = maxDepth - minDepth;
	result.m[3][0] = left + width / 2.0f;
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = minDepth;
	result.m[3][3] = 1.0f;

	return result;
}

// 座標変換
Vector3 Matrix4x4::Transform(const Vector3& vector, const Matrix4x4& matrix) {

	Vector3 result;

	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];

	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];

	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];

	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];

	// w除算
	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}

	return result;
}