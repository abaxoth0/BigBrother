#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_DA_CAP 32

#define DA_GROW(da, n) do {                                             \
    if ((n) <= 0 || (da)->len+(n) <= (da)->cap) break;                  \
    size_t new_cap = (da)->cap;                                         \
    if (new_cap <= 0) new_cap = DEFAULT_DA_CAP;                         \
    while ((da)->len+n > new_cap) new_cap *= 2;                         \
    void *tmp = realloc((da)->elems, new_cap * sizeof(*(da)->elems));   \
    if (!tmp) {                                                         \
        /* TODO Need to somehow properly handle this */                 \
        assert(0 && "Memory reallocation failed (out of RAM?)");        \
        break;                                                          \
    }                                                                   \
    (da)->elems = tmp;                                                  \
    (da)->cap = new_cap;                                                \
} while(0)


// TODO: Now max string view size is restricted

// Note: `len` doesn't count null terminator.
typedef struct {
    size_t cap;
    size_t len;
    char* elems;
} StringView;

// Creates new string view. If `from` is not NULL then uses its copy for the initial string view value.
StringView NewStringView(char* from, size_t cap);
// Releases underlying char buffer (`elems`). Sets `cap`, `len` to 0 and `elems` to NULL.
void StringViewFree(StringView *str);
// Appends specified char buffer to a string view.
void StringViewAppend(StringView *str, char* buf);
// Sets string view length to 0 and null terminates first char.
void StringViewClear(StringView *str);

#endif
