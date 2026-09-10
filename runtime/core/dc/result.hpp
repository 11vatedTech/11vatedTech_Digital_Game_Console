// dc/result.hpp — Platform result type (DC-CANON-001 §16; ADR-0003)
// runtime/core has no host dependencies (canon §39.1).
#pragma once

#include <string>
#include <utility>

namespace dc {

enum class ErrorCode {
    Ok = 0,
    Unavailable,
    AccessDenied,
    InvalidState,
    NotImplemented,
    InternalError,
};

struct Result {
    ErrorCode code = ErrorCode::Ok;
    std::string message;

    Result() = default;
    Result(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}

    explicit operator bool() const { return code == ErrorCode::Ok; }
    bool ok() const { return code == ErrorCode::Ok; }

    static Result Success() { return Result{}; }
    static Result Fail(ErrorCode c, std::string msg) { return Result{c, std::move(msg)}; }
    static Result Unavailable(std::string msg = "capability unavailable on this host") {
        return Result{ErrorCode::Unavailable, std::move(msg)};
    }
    static Result NotImplemented(std::string msg = "not implemented") {
        return Result{ErrorCode::NotImplemented, std::move(msg)};
    }
};

} // namespace dc
