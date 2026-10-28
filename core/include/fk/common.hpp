// FrameKeeper core — common types (API-SPEC-core §1–2).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace fk {

enum class Errc : int {
    ok = 0,
    invalid_argument = 1,
    io_open = 10,
    io_read = 11,
    io_write = 12,
    disk_full = 13,
    exists = 14,
    not_found = 15,
    unsupported_format = 20,
    damaged_file = 21,
    color_profile = 30,
    cancelled = 40,
    memory_budget = 41,
    capability = 42,
    needs_review = 50,
    needs_confirmation = 51,
    conflict = 52,
    version_too_new = 60,
    parse_error = 61,
    internal = 99,
};

/// Stable snake_case name used in JSON and logs.
const char* errcName(Errc code);

struct Error {
    Errc code = Errc::internal;
    std::string message; ///< user-presentable, English
    std::string detail;  ///< technical detail for logs
};

inline Error makeError(Errc c, std::string msg, std::string detail = {}) {
    return Error{c, std::move(msg), std::move(detail)};
}

/// Value-or-error. No exceptions cross the core API.
template <class T> class Result {
  public:
    Result(T value) : v_(std::move(value)) {}     // NOLINT(google-explicit-constructor)
    Result(Error error) : v_(std::move(error)) {} // NOLINT(google-explicit-constructor)
    bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }
    T& value() & { return std::get<0>(v_); }
    const T& value() const& { return std::get<0>(v_); }
    T&& value() && { return std::get<0>(std::move(v_)); }
    T* operator->() { return &std::get<0>(v_); }
    const T* operator->() const { return &std::get<0>(v_); }
    T& operator*() & { return std::get<0>(v_); }
    const T& operator*() const& { return std::get<0>(v_); }
    T&& operator*() && { return std::get<0>(std::move(v_)); }
    const Error& error() const { return std::get<1>(v_); }

  private:
    std::variant<T, Error> v_;
};

template <> class Result<void> {
  public:
    Result() = default;
    Result(Error error) : e_(std::move(error)) {} // NOLINT(google-explicit-constructor)
    bool ok() const { return !e_.has_value(); }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return *e_; }

  private:
    std::optional<Error> e_;
};

using Status = Result<void>;

#define FK_TRY(expr)                                                                                         \
    do {                                                                                                     \
        auto&& fk_try_r_ = (expr);                                                                           \
        if (!fk_try_r_.ok()) return fk_try_r_.error();                                                       \
    } while (0)

/// Cooperative cancellation, polled by long loops at least every 64 rows.
class CancelToken {
  public:
    void cancel() { flag_.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return flag_.load(std::memory_order_relaxed); }
    void reset() { flag_.store(false, std::memory_order_relaxed); }

  private:
    std::atomic<bool> flag_{false};
};

/// Progress callback: fraction in [0,1] and a short stage name. May be empty.
using ProgressFn = std::function<void(double fraction, std::string_view stage)>;

inline void report(const ProgressFn& fn, double f, std::string_view stage) {
    if (fn) fn(f, stage);
}

inline Error cancelledError() {
    return makeError(Errc::cancelled, "Operation cancelled");
}

struct SizeI {
    int64_t w = 0;
    int64_t h = 0;
    friend bool operator==(const SizeI&, const SizeI&) = default;
};

struct RectI {
    int64_t x = 0, y = 0, w = 0, h = 0;
    int64_t right() const { return x + w; }
    int64_t bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    friend bool operator==(const RectI&, const RectI&) = default;
};

} // namespace fk
