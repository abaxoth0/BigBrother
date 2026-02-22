#include "../include/common.h"
#include <string.h>

StringView NewStringView(char* from, size_t cap) {
    StringView str = {0};
    size_t from_size = from ? strlen(from) : 0;
    size_t str_cap = cap > from_size ? cap : from_size;
    DA_GROW(&str, str_cap ? str_cap : DEFAULT_DA_CAP);
    if (from) strcpy(str.elems, from);
    return str;
}

void StringViewFree(StringView *str) {
    if (!str) return;
    free(str->elems);
    str->elems = NULL;
    str->cap = 0;
    str->len = 0;
};

void StringViewAppend(StringView *str, char* buf) {
    assert(str && buf);
    size_t new_len = strlen(buf) + str->len;
    DA_GROW(str, new_len + 1);
    strcat(str->elems, buf);
    str->len = new_len;
}

void StringViewClear(StringView *str) {
    assert(str);
    if (str->len) {
        str->elems[0] = '\0';
        str->len = 0;
    }
}
