#pragma once

#include <fmt/ostream.h>

#include <Eigen/Core>

namespace krd {
template <typename T, int N> using Vector = Eigen::Vector<T, N>;
template <typename T, int N, int M> using Matrix = Eigen::Matrix<T, N, M>;

//
template <typename T> using Vector2 = Eigen::Vector2<T>;
template <typename T> using Vector3 = Eigen::Vector3<T>;
template <typename T> using Vector4 = Eigen::Vector4<T>;

using Vector2i = Vector2<int32_t>;
using Vector3i = Vector3<int32_t>;
using Vector4i = Vector4<int32_t>;

using Vector2u = Vector2<uint32_t>;
using Vector3u = Vector3<uint32_t>;
using Vector4u = Vector4<uint32_t>;

using Vector2f = Vector2<float>;
using Vector3f = Vector3<float>;
using Vector4f = Vector4<float>;

using Vector2d = Vector2<double>;
using Vector3d = Vector3<double>;
using Vector4d = Vector4<double>;

//
template <typename T> using Matrix2 = Eigen::Matrix<T, 2, 2>;
template <typename T> using Matrix3 = Eigen::Matrix<T, 3, 3>;
template <typename T> using Matrix4 = Eigen::Matrix<T, 4, 4>;

using Matrix2f = Matrix2<float>;
using Matrix3f = Matrix3<float>;
using Matrix4f = Matrix4<float>;

using Matrix2d = Matrix2<double>;
using Matrix3d = Matrix3<double>;
using Matrix4d = Matrix4<double>;
} // namespace krd

// fmt overloads
template <typename T, int N> struct fmt::formatter<krd::Vector<T, N>> : fmt::ostream_formatter {};
template <typename T, int N, int M>
struct fmt::formatter<krd::Matrix<T, N, M>> : fmt::ostream_formatter {};
