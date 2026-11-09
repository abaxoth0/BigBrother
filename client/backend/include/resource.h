#pragma once

#include <windows.h>
#include <utility>

namespace client {

class Handle {
    HANDLE value_ = nullptr;
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (*this) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
};

class ServiceHandle {
    SC_HANDLE value_;
public:
    explicit ServiceHandle(SC_HANDLE value) : value_(value) {}
    ~ServiceHandle() { if (value_) CloseServiceHandle(value_); }
    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;
    SC_HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
};

}
