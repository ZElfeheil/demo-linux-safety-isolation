#ifndef USERSPACE_COMMON_EXPECTED_HPP
#define USERSPACE_COMMON_EXPECTED_HPP

#if __has_include(<expected>)
#include <expected>
#endif

#if !defined(__cpp_lib_expected)

#include <variant>
#include <utility>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace safety {

template<typename E>
class unexpected {
public:
    constexpr explicit unexpected(E err) : error_(std::move(err)) {}

    template<typename U>
    requires std::is_constructible_v<E, const U&>
    constexpr explicit unexpected(const unexpected<U>& other) : error_(other.error()) {}

    template<typename U>
    requires std::is_constructible_v<E, U>
    constexpr explicit unexpected(unexpected<U>&& other) : error_(std::move(other).error()) {}

    [[nodiscard]] constexpr const E& error() const & noexcept { return error_; }
    [[nodiscard]] constexpr E& error() & noexcept { return error_; }
    [[nodiscard]] constexpr E&& error() && noexcept { return std::move(error_); }

private:
    E error_;
};

template<typename E>
unexpected(E) -> unexpected<E>;

template<typename E>
class bad_expected_access : public std::exception {
public:
    explicit bad_expected_access(E err) : error_(std::move(err)) {}
    [[nodiscard]] const char* what() const noexcept override { return "bad_expected_access"; }
    [[nodiscard]] const E& error() const noexcept { return error_; }
private:
    E error_;
};

template<typename T, typename E>
class expected {
public:
    constexpr expected() : var_(std::in_place_type<T>) {}

    template<typename U = T>
    requires (!std::is_same_v<std::remove_cvref_t<U>, expected> &&
              !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
              std::is_constructible_v<T, U>)
    constexpr expected(U&& val) : var_(std::in_place_type<T>, std::forward<U>(val)) {}

    template<typename G>
    requires std::is_constructible_v<E, const G&>
    constexpr expected(const unexpected<G>& unex) : var_(std::in_place_type<unexpected<E>>, unex.error()) {}

    template<typename G>
    requires std::is_constructible_v<E, G>
    constexpr expected(unexpected<G>&& unex) : var_(std::in_place_type<unexpected<E>>, std::move(unex).error()) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return std::holds_alternative<T>(var_); }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr const T& value() const & {
        if (!has_value()) throw bad_expected_access<E>(error());
        return std::get<T>(var_);
    }
    [[nodiscard]] constexpr T& value() & {
        if (!has_value()) throw bad_expected_access<E>(error());
        return std::get<T>(var_);
    }

    [[nodiscard]] constexpr const T& operator*() const & noexcept { return std::get<T>(var_); }
    [[nodiscard]] constexpr T& operator*() & noexcept { return std::get<T>(var_); }
    [[nodiscard]] constexpr const T* operator->() const noexcept { return &std::get<T>(var_); }
    [[nodiscard]] constexpr T* operator->() noexcept { return &std::get<T>(var_); }

    [[nodiscard]] constexpr const E& error() const & noexcept { return std::get<unexpected<E>>(var_).error(); }
    [[nodiscard]] constexpr E& error() & noexcept { return std::get<unexpected<E>>(var_).error(); }

private:
    std::variant<T, unexpected<E>> var_;
};

// Void specialization for safety::expected<void, E>
template<typename E>
class expected<void, E> {
public:
    constexpr expected() : var_(std::in_place_type<std::monostate>) {}

    template<typename G>
    requires std::is_constructible_v<E, const G&>
    constexpr expected(const unexpected<G>& unex) : var_(std::in_place_type<unexpected<E>>, unex.error()) {}

    template<typename G>
    requires std::is_constructible_v<E, G>
    constexpr expected(unexpected<G>&& unex) : var_(std::in_place_type<unexpected<E>>, std::move(unex).error()) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return std::holds_alternative<std::monostate>(var_); }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr void value() const {
        if (!has_value()) throw bad_expected_access<E>(error());
    }

    [[nodiscard]] constexpr const E& error() const & noexcept { return std::get<unexpected<E>>(var_).error(); }
    [[nodiscard]] constexpr E& error() & noexcept { return std::get<unexpected<E>>(var_).error(); }

private:
    std::variant<std::monostate, unexpected<E>> var_;
};

} // namespace safety

#endif // !defined(__cpp_lib_expected)

#endif // USERSPACE_COMMON_EXPECTED_HPP
