/// @file strong_type.hpp
/// @brief Tagged wrapper that makes distinct IDs distinct types (TODO §3.4).
///
/// `StrongType<std::uint32_t, struct TopicIdTag>` and `StrongType<std::uint32_t, struct NodeIdTag>`
/// share a representation but cannot be mixed up: construction is explicit, there is no implicit
/// conversion back, and two different tags never compare. The wrapper is trivially copyable and has
/// the same size as the value, so it is safe in shared memory and on the wire.
#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace fastbus::core {

/// Tagged value type. `T` must be trivially copyable; `Tag` is any (usually incomplete) type.
/// Thread-safety: a plain value; same rules as `T`.
template <typename T, typename Tag>
    requires std::is_trivially_copyable_v<T>
class StrongType {
public:
    using ValueType = T;

    /// Value-initialized (zero for arithmetic types).
    constexpr StrongType() noexcept = default;
    constexpr explicit StrongType(T value) noexcept : value_{value} {}

    /// The wrapped value. Use only at boundaries (formatting, serialization, hashing).
    [[nodiscard]] constexpr T value() const noexcept { return value_; }

    friend constexpr bool operator==(const StrongType&, const StrongType&) noexcept = default;
    friend constexpr auto operator<=>(const StrongType&, const StrongType&) noexcept = default;

private:
    T value_{};
};

}  // namespace fastbus::core

/// Hashing: lets strong types be keys in unordered containers. Specializing std::hash for a
/// user-defined type is explicitly permitted by the standard ([namespace.std]/2).
template <typename T, typename Tag>
struct std::hash<fastbus::core::StrongType<T, Tag>> {  // NOLINT(cert-dcl58-cpp): allowed, see above
    [[nodiscard]] std::size_t operator()(fastbus::core::StrongType<T, Tag> id) const noexcept {
        return std::hash<T>{}(id.value());
    }
};
