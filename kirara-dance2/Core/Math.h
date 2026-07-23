#pragma once

#include <kira/Properties.h>

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
template <typename T, int N, int M>
struct fmt::formatter<krd::Matrix<T, N, M>> : fmt::ostream_formatter {};

namespace kira {
template <typename T, int N, int M>
struct PropertyProcessor<::krd::Matrix<T, N, M>> : std::true_type {
    static constexpr std::string_view name = "krd::Matrix";

    static auto to_toml(::krd::Matrix<T, N, M> const &mat) {
        toml::array arr;
        // Should use mat.rows() instead of N since Eigen allows for dynamic sizes.
        for (int i = 0; i < mat.rows(); ++i) {
            toml::array row;
            for (int j = 0; j < mat.cols(); ++j)
                row.push_back(mat(i, j));
            arr.push_back(row);
        }

        return arr;
    }

    static auto from_toml(toml::node &node, auto const &owner) {
        toml::array *arr = node.as_array();
        if (!arr)
            throw Anyhow("Expected an array, but got {}", magic_enum::enum_name(node.type()));

        ::krd::Matrix<T, N, M> mat;
        if (!can_eq(arr->size(), N) || arr->size() == 0) {
            if (N == Eigen::Dynamic)
                throw Anyhow("Expected a non-empty array, but got an empty array");
            throw Anyhow("Expected {} row(s), but got {}", N, arr->size());
        }

        toml::array *arr0 = arr->at(0).as_array();
        if (!arr0)
            throw Anyhow(
                "Expected an array for row 0, but got {}",
                magic_enum::enum_name(arr->at(0).type())
            );

        auto const rows = arr->size();
        auto const cols = arr0->size();
        if (!can_eq(arr0->size(), M) || arr0->size() == 0) {
            if (M == Eigen::Dynamic)
                throw Anyhow("Row 0 is empty");
            throw Anyhow("Expected {} column(s), but row 0 has {}", M, cols);
        }

        mat.resize(rows, cols);
        for (int i = 0; i < rows; ++i) {
            toml::array *row = arr->at(i).as_array();
            if (!row) {
                throw Anyhow(
                    "Expected an array for row {}, but got {}", i,
                    magic_enum::enum_name(arr->at(i).type())
                );
            }
            if (row->size() != cols) {
                throw Anyhow(
                    "Row {} has {} column(s), but expected {}", i, row->size(), cols
                );
            }
            for (int j = 0; j < cols; ++j) {
                if (auto const &value = row->at(j).template value<T>())
                    mat(i, j) = value.value();
                else
                    throw Anyhow(
                        "Expected a matrix element, but got {} at [{}, {}]",
                        magic_enum::enum_name(row->at(j).type()), i, j
                    );
            }
        }

        return mat;
    }

private:
    static bool can_eq(size_t arr_size, int eig_size) {
        if (eig_size == Eigen::Dynamic)
            return true;
        return arr_size == eig_size;
    }
};
} // namespace kira
