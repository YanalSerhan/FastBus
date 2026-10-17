/// @file result.hpp
/// @brief `Result<T, E>`: a value or an error, without exceptions (ADR-001).
///
/// The API is a strict subset of C++23 `std::expected`, so migrating later is mechanical:
///   Result<SpscRing<int>> r = SpscRing<int>::create(64);
///   if (!r) { log(r.error().message); return; }
///   SpscRing<int>& ring = *r;
/// Return an error with `return fail(ErrorCode::InvalidCapacity, "capacity must be a power of 2");`
/// and success of `Result<void>` with `return {};`.
///
/// Used for construction and setup only; hot-path operations return bool / pointers.
/// Accessing the wrong alternative (`value()` on an error, `error()` on a value) is a contract
/// violation and terminates through `fail_fast` in every build type. It never throws.
#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "fastbus/core/assert.hpp"
#include "fastbus/core/error.hpp"

namespace fastbus::core {

/// Wrapper that marks a value as the error alternative (like std::unexpected).
template <typename E>
class Unexpected {
public:
    constexpr explicit Unexpected(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        : error_{std::move(error)} {}
    [[nodiscard]] constexpr const E& error() const& noexcept { return error_; }
    [[nodiscard]] constexpr E&& error() && noexcept { return std::move(error_); }

private:
    E error_;
};

template <typename E>
Unexpected(E) -> Unexpected<E>;

/// Shorthand for the common case: `return fail(ErrorCode::X, "static message");`
[[nodiscard]] constexpr Unexpected<Error> fail(ErrorCode code, const char* message) noexcept {
    return Unexpected<Error>{Error{code, message}};
}

/// Either a `T` or an `E`. Thread-safety: same as `T` / `E`; no internal synchronization.
template <typename T, typename E = Error>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result<T&> is not supported; return a pointer");
    static_assert(!std::is_same_v<std::remove_cv_t<T>, E>, "T and E must differ");

public:
    using ValueType = T;
    using ErrorType = E;

    // Implicit on purpose, mirroring std::expected: `return value;` and `return fail(...);`.
    constexpr Result(const T& value)  // NOLINT(google-explicit-constructor)
        requires std::is_copy_constructible_v<T>
        : storage_{std::in_place_index<0>, value} {}
    constexpr Result(T&& value)  // NOLINT(google-explicit-constructor)
        : storage_{std::in_place_index<0>, std::move(value)} {}
    template <typename G>
        requires std::is_constructible_v<E, G>
    constexpr Result(Unexpected<G> error)  // NOLINT(google-explicit-constructor)
        : storage_{std::in_place_index<1>, std::move(error).error()} {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr T& value() & noexcept { return *checked_value(); }
    [[nodiscard]] constexpr const T& value() const& noexcept { return *checked_value(); }
    [[nodiscard]] constexpr T&& value() && noexcept { return std::move(*checked_value()); }

    [[nodiscard]] constexpr const E& error() const& noexcept {
        const E* e = std::get_if<1>(&storage_);
        check(e != nullptr, "Result::error() called on a value");
        return *e;
    }

    [[nodiscard]] constexpr T& operator*() & noexcept { return value(); }
    [[nodiscard]] constexpr const T& operator*() const& noexcept { return value(); }
    [[nodiscard]] constexpr T&& operator*() && noexcept { return std::move(*this).value(); }
    [[nodiscard]] constexpr T* operator->() noexcept { return checked_value(); }
    [[nodiscard]] constexpr const T* operator->() const noexcept { return checked_value(); }

    /// The value, or `fallback` converted to T when this holds an error.
    template <typename U>
    [[nodiscard]] constexpr T value_or(U&& fallback) const& {
        return has_value() ? *std::get_if<0>(&storage_) : static_cast<T>(std::forward<U>(fallback));
    }

private:
    [[nodiscard]] constexpr T* checked_value() noexcept {
        T* v = std::get_if<0>(&storage_);
        check(v != nullptr, "Result::value() called on an error");
        return v;
    }
    [[nodiscard]] constexpr const T* checked_value() const noexcept {
        const T* v = std::get_if<0>(&storage_);
        check(v != nullptr, "Result::value() called on an error");
        return v;
    }

    std::variant<T, E> storage_;
};

/// Success-or-error with no value. Default-constructed means success: `return {};`
template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    using ValueType = void;
    using ErrorType = E;

    constexpr Result() noexcept = default;
    template <typename G>
        requires std::is_constructible_v<E, G>
    constexpr Result(Unexpected<G> error)  // NOLINT(google-explicit-constructor)
        : error_{std::move(error).error()} {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return !error_.has_value(); }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    /// Asserts success (terminates otherwise). Mirrors std::expected<void, E>::value().
    constexpr void value() const noexcept {
        check(has_value(), "Result<void>::value() called on an error");
    }
    [[nodiscard]] constexpr const E& error() const& noexcept {
        if (!error_.has_value()) [[unlikely]] {
            fail_fast("Result<void>::error() called on success");
        }
        return *error_;
    }

private:
    std::optional<E> error_;
};

}  // namespace fastbus::core
