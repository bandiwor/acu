#include "parser/parse_number.h"
#include "literal/literal.h"

static const f64 FAST_POW10[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                 1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

static const f64 FAST_NEG_POW10[] = {1e-0,  1e-1,  1e-2,  1e-3,  1e-4,  1e-5,  1e-6,  1e-7,
                                     1e-8,  1e-9,  1e-10, 1e-11, 1e-12, 1e-13, 1e-14, 1e-15,
                                     1e-16, 1e-17, 1e-18, 1e-19, 1e-20, 1e-21, 1e-22};

static inline f64 FastPow10(i32 exp) {
    if (likely(exp >= 0 && exp <= 22))
        return FAST_POW10[exp];
    if (likely(exp < 0 && exp >= -22))
        return FAST_NEG_POW10[-exp];

    f64 res = 1.0;
    f64 base = exp > 0 ? 10.0 : 0.1;
    i32 abs_exp = exp > 0 ? exp : -exp;
    for (i32 i = 0; i < abs_exp; i++)
        res *= base;
    return res;
}

static bool ParseNumericSuffix(StringView sv, bool is_float_parsed, u32 *out_kind,
                               bool *force_float) {
    *force_float = false;

    if (sv.length == 2) {
        if (sv.data[0] == 'i' && sv.data[1] == '1') {
            *out_kind = ACU_INT_SUFFIX_I1;
            return (bool)!is_float_parsed;
        }
        if (sv.data[0] == 'i' && sv.data[1] == '8') {
            *out_kind = ACU_INT_SUFFIX_I8;
            return (bool)!is_float_parsed;
        }
        if (sv.data[0] == 'u' && sv.data[1] == '8') {
            *out_kind = ACU_INT_SUFFIX_U8;
            return (bool)!is_float_parsed;
        }
    } else if (sv.length == 3) {
        if (sv.data[0] == 'i') {
            if (sv.data[1] == '1' && sv.data[2] == '6') {
                *out_kind = ACU_INT_SUFFIX_I16;
                return (bool)!is_float_parsed;
            }
            if (sv.data[1] == '3' && sv.data[2] == '2') {
                *out_kind = ACU_INT_SUFFIX_I32;
                return (bool)!is_float_parsed;
            }
            if (sv.data[1] == '6' && sv.data[2] == '4') {
                *out_kind = ACU_INT_SUFFIX_I64;
                return (bool)!is_float_parsed;
            }
        } else if (sv.data[0] == 'u') {
            if (sv.data[1] == '1' && sv.data[2] == '6') {
                *out_kind = ACU_INT_SUFFIX_U16;
                return (bool)!is_float_parsed;
            }
            if (sv.data[1] == '3' && sv.data[2] == '2') {
                *out_kind = ACU_INT_SUFFIX_U32;
                return (bool)!is_float_parsed;
            }
            if (sv.data[1] == '6' && sv.data[2] == '4') {
                *out_kind = ACU_INT_SUFFIX_U64;
                return (bool)!is_float_parsed;
            }
        } else if (sv.data[0] == 'f') {
            if (sv.data[1] == '3' && sv.data[2] == '2') {
                *out_kind = ACU_FLOAT_SUFFIX_F32;
                *force_float = true;
                return true;
            }
            if (sv.data[1] == '6' && sv.data[2] == '4') {
                *out_kind = ACU_FLOAT_SUFFIX_F64;
                *force_float = true;
                return true;
            }
        }
    }

    return false;
}

AcuNumericResult Acu_ParseNumericLiteral(StringView sv) {
    AcuNumericResult result = {false};
    u32 i = 0;

    u64 int_part = 0;
    while (i < sv.length && sv.data[i] >= '0' && sv.data[i] <= '9') {
        u8 digit = sv.data[i] - '0';

        if (unlikely(int_part > 1844674407370955161ULL ||
                     (int_part == 1844674407370955161ULL && digit > 5))) {
            result.is_overflow = true;
        }
        int_part = (int_part * 10) + digit;
        i++;
    }

    if ((i < sv.length && sv.data[i] == '.') ||
        (i < sv.length && (sv.data[i] == 'e' || sv.data[i] == 'E'))) {

        result.is_float = true;
        f64 float_val = (f64)int_part;
        i32 frac_exp = 0;

        if (sv.data[i] == '.') {
            i++;
            while (i < sv.length && sv.data[i] >= '0' && sv.data[i] <= '9') {
                float_val = (float_val * 10.0) + (sv.data[i] - '0');
                frac_exp--;
                i++;
            }
        }

        i32 scientific_exp = 0;
        if (i < sv.length && (sv.data[i] == 'e' || sv.data[i] == 'E')) {
            i++;
            bool exp_negative = false;
            if (i < sv.length && (sv.data[i] == '+' || sv.data[i] == '-')) {
                exp_negative = (sv.data[i] == '-');
                i++;
            }
            while (i < sv.length && sv.data[i] >= '0' && sv.data[i] <= '9') {
                scientific_exp = (scientific_exp * 10) + (sv.data[i] - '0');
                i++;
            }
            if (exp_negative)
                scientific_exp = -scientific_exp;
        }

        i32 total_exp = frac_exp + scientific_exp;
        if (total_exp != 0) {
            float_val *= FastPow10(total_exp);
        }

        result.as.f64 = float_val;
    } else {
        result.as.u64 = int_part;
    }

    if (i < sv.length) {
        result.has_suffix = true;
        StringView suffix_sv = {.data = sv.data + i, .length = sv.length - i};

        bool force_float = false;
        result.is_suffix_valid =
            ParseNumericSuffix(suffix_sv, result.is_float, &result.suffix_kind, &force_float);

        if (force_float && !result.is_float) {
            result.as.f64 = (f64)result.as.u64;
            result.is_float = true;
        }
    }

    return result;
}
