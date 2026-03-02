/** @file common.h
 * @brief Common utilities for BigBrother firewall.
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

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
    size_t cap;
    size_t len;
    char* elems;
} StringView;

/** @brief Creates new string view. */
StringView NewStringView(char* from, size_t cap);

/** @brief Releases underlying char buffer. */
void StringViewFree(StringView *str);

/** @brief Appends specified char buffer to a string view. */
void StringViewAppend(StringView *str, char* buf);

/** @brief Appends multiple NULL-terminated char* buffers to str. */
void StringViewAppendV(StringView *str, ...);

/** @brief Sets string view length to 0 and null terminates first char. */
void StringViewClear(StringView *str);

/** @brief Convert string to lowercase (ASCII, in-place). */
void to_lower_inplace(char* str);

#endif
