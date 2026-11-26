/**
 * @file windows_handle.h
 * @brief Scoped ownership of Windows kernel handles.
 */

#pragma once

#include <windows.h>
#include <utility>

namespace bb {

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

}
