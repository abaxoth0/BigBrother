/**
 * @file resource.h
 * @brief Scoped Windows resource ownership.
 */

#pragma once

#include "windows_handle.h"

namespace client {

using bb::Handle;

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
