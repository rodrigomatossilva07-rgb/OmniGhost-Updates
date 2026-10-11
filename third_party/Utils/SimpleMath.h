// Minimal DirectX SimpleMath compatibility layer used by OmniGhost.
// It intentionally implements only the types and operations referenced by this project.
#pragma once

#include <DirectXMath.h>

namespace DirectX::SimpleMath {

struct Vector3 : public XMFLOAT3 {
    Vector3() noexcept : XMFLOAT3(0.f, 0.f, 0.f) {}
    Vector3(float ix, float iy, float iz) noexcept : XMFLOAT3(ix, iy, iz) {}
    explicit Vector3(const float* values) noexcept : XMFLOAT3(values) {}
    Vector3(FXMVECTOR value) noexcept { XMStoreFloat3(this, value); }
    Vector3(const XMFLOAT3& value) noexcept : XMFLOAT3(value) {}

    operator XMVECTOR() const noexcept { return XMLoadFloat3(this); }
};

struct Vector4 : public XMFLOAT4 {
    Vector4() noexcept : XMFLOAT4(0.f, 0.f, 0.f, 0.f) {}
    Vector4(float ix, float iy, float iz, float iw) noexcept : XMFLOAT4(ix, iy, iz, iw) {}
    explicit Vector4(const float* values) noexcept : XMFLOAT4(values) {}
    Vector4(FXMVECTOR value) noexcept { XMStoreFloat4(this, value); }
    Vector4(const XMFLOAT4& value) noexcept : XMFLOAT4(value) {}

    operator XMVECTOR() const noexcept { return XMLoadFloat4(this); }
};

struct Matrix : public XMFLOAT4X4 {
    Matrix() noexcept
        : XMFLOAT4X4(
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f) {}

    Matrix(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33) noexcept
        : XMFLOAT4X4(
            m00, m01, m02, m03,
            m10, m11, m12, m13,
            m20, m21, m22, m23,
            m30, m31, m32, m33) {}

    Matrix(const XMFLOAT4X4& value) noexcept : XMFLOAT4X4(value) {}
    Matrix(CXMMATRIX value) noexcept { XMStoreFloat4x4(this, value); }

    operator XMMATRIX() const noexcept { return XMLoadFloat4x4(this); }

    Matrix Transpose() const noexcept {
        return Matrix(XMMatrixTranspose(XMLoadFloat4x4(this)));
    }
};

static_assert(sizeof(Vector3) == sizeof(XMFLOAT3));
static_assert(sizeof(Vector4) == sizeof(XMFLOAT4));
static_assert(sizeof(Matrix) == sizeof(XMFLOAT4X4));

} // namespace DirectX::SimpleMath

#include "SimpleMath.inl"
