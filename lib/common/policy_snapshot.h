/**
 * @file policy_snapshot.h
 * @brief In-memory delivery of bounded opaque policy text from a single publisher to desktop applications.
 */
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace bb {
/** @brief Layout shared by the service's writable view and the application's read-only view on Windows x64. */
struct PolicySnapshot {
    static constexpr size_t capacity = 8192;
    std::uint32_t revision;
    std::uint32_t length;
    char payload[capacity];

    /** @brief Publish from one thread. Readers use only stable, even revisions. */
    bool publish(std::string_view bytes) {
        if (bytes.size() > capacity) {
            return false;
        }
        std::atomic_ref sequence(revision);
        sequence.fetch_add(1);
        std::atomic_ref(length).store(static_cast<std::uint32_t>(bytes.size()));
        for (size_t i = 0; i < bytes.size(); ++i) {
            std::atomic_ref(payload[i]).store(bytes[i]);
        }
        sequence.fetch_add(1);
        return true;
    }

    /** @brief Copy a new complete snapshot. A concurrent write or an unchanged revision leaves output intact. */
    bool read(std::uint32_t& previous, std::string& bytes) const {
        const std::atomic_ref sequence(revision);
        auto current = sequence.load();
        if ((current & 1) || current == previous) {
            return false;
        }
        auto size = std::atomic_ref(length).load();
        if (size > capacity) {
            return false;
        }
        std::string value(size, '\0');
        for (size_t i = 0; i < size; ++i) {
            value[i] = std::atomic_ref(payload[i]).load();
        }
        if (sequence.load() != current) {
            return false;
        }
        previous = current;
        bytes.swap(value);
        return true;
    }
};

static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free &&
              std::atomic_ref<char>::is_always_lock_free);

/** @brief Own one mapped view; the mapping handle can be closed once the view is open. */
template <typename T> class PolicyView {
    T* view_ = nullptr;

public:
    /** @brief Start with no mapping. */
    PolicyView() = default;
    PolicyView(const PolicyView&) = delete;
    PolicyView& operator=(const PolicyView&) = delete;
    /** @brief Release the view on scope exit. */
    ~PolicyView() {
        if (view_) {
            UnmapViewOfFile(view_);
        }
    }
    /** @brief Map exactly one snapshot with either FILE_MAP_READ or FILE_MAP_WRITE. */
    bool open(HANDLE mapping, DWORD access) {
        if (view_) {
            return false;
        }
        view_ = static_cast<T*>(MapViewOfFile(mapping, access, 0, 0, sizeof(PolicySnapshot)));
        return view_ != nullptr;
    }
    /** @brief Borrow the current view, or nullptr before open succeeds. */
    T* get() const {
        return view_;
    }
};
}
