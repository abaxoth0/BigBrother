/** @file common.h
 * @brief Common data structures and utilities for BigBrother firewall.
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>

#define DEFAULT_DA_CAP 32
#define MAX_WHITELIST_DOMAINS 256
#define MAX_DOMAIN_LEN 256
#define MAX_ALLOWED_IPS 1024

#define DEFAULT_DA_CAP 32

#define DA_GROW(da, n) do { \
    if ((n) <= 0 || (da)->len+(n) <= (da)->cap) break; \
    size_t _new_cap = (da)->cap; \
    if (_new_cap <= 0) _new_cap = DEFAULT_DA_CAP; \
    while ((da)->len+(n) > _new_cap) { \
        assert(_new_cap && "size_t overflow"); \
        _new_cap *= 2; \
    } \
    void *_tmp = realloc((da)->elems, _new_cap * sizeof(*(da)->elems)); \
    if (!_tmp) { \
        /* TODO Need to somehow properly handle this */ \
        assert(0 && "Memory reallocation failed (out of RAM?)"); \
        break; \
    } \
    (da)->elems = _tmp; \
    (da)->cap = _new_cap; \
} while(0)


/** @brief Dynamic string container.
 *
 * Note: `len` doesn't count null terminator.
 */
typedef struct {
    size_t cap;      ///< Buffer capacity.
    size_t len;      ///< String length (excluding null terminator).
    char* elems;     ///< Character buffer.
} StringView;

/** @brief Creates new string view.
 * @param[in] from Initial string (can be NULL).
 * @param[in] cap  Initial capacity.
 * @return Initialized StringView.
 */
StringView NewStringView(char* from, size_t cap);

/** @brief Releases underlying char buffer.
 * @param[in,out] str StringView to free. Sets cap, len to 0 and elems to NULL.
 */
void StringViewFree(StringView *str);

/** @brief Appends specified char buffer to a string view.
 * @param[in,out] str StringView to append to.
 * @param[in]     buf Buffer to append.
 */
void StringViewAppend(StringView *str, char* buf);

/** @brief Appends multiple NULL-terminated char* buffers to str.
 * @param[in,out] str StringView to append to.
 * @param[in]     ... Variable number of NULL-terminated char* arguments.
 *
 * Example: StringViewAppendV(&str, "Hello", ", ", "World!", NULL);
 */
void StringViewAppendV(StringView *str, ...);

/** @brief Sets string view length to 0 and null terminates first char.
 * @param[in,out] str StringView to clear.
 */
void StringViewClear(StringView *str);

#endif
