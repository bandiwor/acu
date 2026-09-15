#include "types/string_view.h"
#include "defines/defines.h"
#include <stddef.h>

StringView StringView_Create(const u8 *data, u64 length) {
    return (StringView){
        .data = data,
        .length = length,
    };
}

attribute_pure bool StringView_Equals(StringView a, StringView b) {
    if (a.length != b.length) {
        return false;
    }
    if (a.length == 0) {
        return true;
    }
    if (a.data == b.data) {
        return true;
    }

    return __builtin_memcmp(a.data, b.data, a.length) == 0;
}

attribute_pure bool StringView_StartsWith(StringView sv, StringView prefix) {
    if (unlikely(prefix.length > sv.length)) {
        return false;
    }
    if (unlikely(prefix.length == 0)) {
        return true;
    }

    return __builtin_memcmp(sv.data, prefix.data, prefix.length) == 0;
}

attribute_pure bool StringView_EndsWith(StringView sv, StringView suffix) {
    if (unlikely(suffix.length > sv.length)) {
        return false;
    }
    if (unlikely(suffix.length == 0)) {
        return true;
    }

    return __builtin_memcmp(sv.data + sv.length - suffix.length, suffix.data, suffix.length) == 0;
}

attribute_pure StringView StringView_Substr(StringView sv, u64 start, u64 length) {
    if (unlikely(start >= sv.length)) {
        return (StringView){.data = sv.data + sv.length, .length = 0};
    }
    u64 actual_len = (start + length > sv.length) ? (sv.length - start) : length;
    return (StringView){.data = sv.data + start, .length = actual_len};
}

attribute_pure StringView StringView_ChopLeft(StringView sv, u64 n) {
    if (unlikely(n >= sv.length)) {
        return (StringView){.data = sv.data + sv.length, .length = 0};
    }
    return (StringView){.data = sv.data + n, .length = sv.length - n};
}

attribute_pure StringView StringView_ChopRight(StringView sv, u64 n) {
    if (unlikely(n >= sv.length)) {
        return (StringView){.data = sv.data, .length = 0};
    }
    return (StringView){.data = sv.data, .length = sv.length - n};
}

attribute_pure StringView StringView_Trim(StringView sv) {
    const u8 *start = sv.data;
    const u8 *end = sv.data + sv.length;

    while (start < end) {
        u8 c = *start;
        if (c == ' ' || (c >= '\t' && c <= '\r'))
            start++;
        else
            break;
    }

    while (end > start) {
        u8 c = *(end - 1);
        if (c == ' ' || (c >= '\t' && c <= '\r'))
            end--;
        else
            break;
    }

    return (StringView){.data = start, .length = (u64)(end - start)};
}

bool StringView_Split(StringView *sv, u8 delimiter, StringView *out_part) {
    if (unlikely(sv->length == 0)) {
        return false;
    }

    const u8 *ptr = __builtin_memchr(sv->data, delimiter, sv->length);
    if (ptr) {
        u64 delim_pos = (u64)(ptr - sv->data);
        *out_part = (StringView){.data = sv->data, .length = delim_pos};

        sv->data += delim_pos + 1;
        sv->length -= delim_pos + 1;
    } else {
        *out_part = *sv;

        sv->data += sv->length;
        sv->length = 0;
    }
    return true;
}

attribute_pure u64 StringView_Hash(StringView sv) {
    const u64 K = 0x517cc1b727220a95ULL;
    u64 hash = 0xcbf29ce484222325ULL;

    const u8 *data = sv.data;
    size_t len = sv.length;

    while (len >= 8) {
        u64 word;
        __builtin_memcpy(&word, data, 8);

        hash = (hash ^ word) * K;

        data += 8;
        len -= 8;
    }

    if (len >= 4) {
        u32 word;
        __builtin_memcpy(&word, data, 4);
        hash = (hash ^ word) * K;
        data += 4;
        len -= 4;
    }
    if (len >= 2) {
        u16 word;
        __builtin_memcpy(&word, data, 2);
        hash = (hash ^ word) * K;
        data += 2;
        len -= 2;
    }
    if (len > 0) {
        hash = (hash ^ data[0]) * K;
    }

    hash ^= sv.length;

    hash ^= hash >> 32;
    hash *= K;
    hash ^= hash >> 32;

    return hash;
}

attribute_nonnull(1) StringView StringView_CStr(const char *c_str) {
    return (StringView){.data = (u8 *)c_str, .length = __builtin_strlen(c_str)};
}

attribute_nonnull(1, 2) StringView StringView_FromCStr(AcuArena *arena, const char *c_str) {
    u64 len = __builtin_strlen(c_str);

    if (unlikely(len == 0)) {
        return (StringView){.data = (const u8 *)"", .length = 0};
    }

    u8 *buffer = AcuArena_AllocateAligned(arena, len, 1);
    __builtin_memcpy(buffer, c_str, len);

    return (StringView){.data = buffer, .length = len};
}

attribute_nonnull(1, 2) StringView
    StringView_FromBuffer(AcuArena *arena, const u8 *data, u64 length) {
    if (unlikely(length == 0)) {
        return (StringView){.data = (const u8 *)"", .length = 0};
    }

    u8 *buffer = AcuArena_AllocateAligned(arena, length, 1);
    __builtin_memcpy(buffer, data, length);

    return (StringView){.data = buffer, .length = length};
}

attribute_nonnull(1) StringView StringView_Clone(AcuArena *arena, StringView other) {
    return StringView_FromBuffer(arena, other.data, other.length);
}

attribute_const StringView StringView_Empty(void) {
    return (StringView){0};
}
