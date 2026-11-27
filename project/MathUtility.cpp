#include "MathUtility.h"

namespace MathUtility {
	Matrix4x4 MakeUVTransformMatrix(const Vector3& scale, float rotateZ, const Vector3& translate) {
		Matrix4x4 mScale = Matrix4x4::MakeScaleMatrix({ scale.x, scale.y, 1.0f });
		Matrix4x4 mRotZ = Matrix4x4::MakeRotateZMatrix(rotateZ);
		Matrix4x4 mTranslate = Matrix4x4::MakeTranslateMatrix({ translate.x, translate.y, 0.0f });
		return Matrix4x4::Multiply(Matrix4x4::Multiply(mScale, mRotZ), mTranslate);
	}
}