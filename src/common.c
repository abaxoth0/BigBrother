#include "../include/common.h"
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

StringView NewStringView(char* from, size_t cap) {
    StringView str = {0};
    size_t from_size = from ? strlen(from) : 0;
    size_t str_cap = cap > from_size ? cap : from_size;
    DA_GROW(&str, str_cap ? str_cap : DEFAULT_DA_CAP);
    if (from) {
        strcpy(str.elems, from);
        str.len = from_size;
    } else {
        str.elems[0] = '\0';
    }
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

void StringViewAppendV(StringView *str, ...) {
    va_list args;
    va_start(args, str);

    char *buf;
    while ((buf = va_arg(args, char*)) != NULL) {
        StringViewAppend(str, buf);
    }

    va_end(args);
};

void StringViewClear(StringView *str) {
    assert(str);
    if (str->len) {
        str->elems[0] = '\0';
        str->len = 0;
    }
}

static void to_lower_inplace(char* str) {
    for (int i = 0; str[i]; i++) {
        str[i] = (char)tolower((unsigned char)str[i]);
    }
}

