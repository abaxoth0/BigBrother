/**
 * @file resource.h
 * @brief Scoped Windows resource ownership.
 */

#pragma once

#include <windows.h>
#include <utility>

namespace client {

/** @brief Move-only owner of a Windows handle; closes valid handles on destruction. */
class Handle {
    HANDLE value_ = nullptr;

public:
    /** @brief Adopt a handle, including null/INVALID_HANDLE_VALUE as an empty owner. */
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    /** @brief Close the owned valid handle; empty and moved-from instances do nothing. */
    ~Handle() {
        if (*this) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    /** @brief Transfer ownership from other and leave it empty. */
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    /** @brief Borrow the handle without transferring ownership; do not close it independently. */
    HANDLE get() const {
        return value_;
    }
    /** @brief Test whether the stored handle is neither null nor INVALID_HANDLE_VALUE. */
    explicit operator bool() const {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }
};

/** @brief Noncopyable owner of an SCM/service handle, released with CloseServiceHandle. */
class ServiceHandle {
    SC_HANDLE value_;

public:
    /** @brief Adopt an SCM/service handle or null for an empty owner. */
    explicit ServiceHandle(SC_HANDLE value) : value_(value) {}
    /** @brief Close the owned non-null handle with CloseServiceHandle(). */
    ~ServiceHandle() {
        if (value_) {
            CloseServiceHandle(value_);
        }
    }
    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;
    /** @brief Borrow the handle without transferring ownership; do not close it independently. */
    SC_HANDLE get() const {
        return value_;
    }
    /** @brief Test whether this owner holds a non-null service handle. */
    explicit operator bool() const {
        return value_ != nullptr;
    }
};

}
