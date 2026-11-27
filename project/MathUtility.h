#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"

namespace MathUtility {
	Matrix4x4 MakeUVTransformMatrix(const Vector3& scale, float rotateZ, const Vector3& translate);
}