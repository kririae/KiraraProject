#pragma once

#include <cstddef>
#include <span>

namespace kira {
template <typename Scalar, std::size_t Size, std::size_t alignment>
struct VecteurStorage {
  alignas(alignment) Scalar storage[Size];

  /// Check if the storage is dynamic.
  [[nodiscard]] static constexpr bool is_dynamic() { return false; }

public:
  [[nodiscard]] constexpr size_t size() const { return Size; }

  [[nodiscard]] constexpr Scalar const *data() const { return storage; }
  [[nodiscard]] constexpr Scalar *data() { return storage; }

  [[nodiscard]] constexpr auto begin() const { return data(); }
  [[nodiscard]] constexpr auto begin() { return data(); }

  [[nodiscard]] constexpr auto end() const { return data() + size(); }
  [[nodiscard]] constexpr auto end() { return data() + size(); }
};

template <typename Scalar, std::size_t alignment>
struct VecteurStorage<Scalar, std::dynamic_extent, alignment> {
  Scalar *storage{nullptr};
  std::size_t size{0};

  /// Check if the storage is dynamic.
  [[nodiscard]] static constexpr bool is_dynamic() { return true; }

public:
  // TODO(krr): implement a copyable heap storage class.
};
} // namespace kira
