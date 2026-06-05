/*
 * Original work:
 * Copyright (c) 2022 Martijn van Welie
 *
 * Modifications:
 * Copyright (c) 2026 Leon.
 *
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>
#include <gio/gio.h>
#include "lm_parser.h"
#include "math.h"
#include <time.h>

// IEEE 11073 Reserved float values
typedef enum {
    MDER_POSITIVE_INFINITY = 0x007FFFFE,
    MDER_NaN = 0x007FFFFF,
    MDER_NRes = 0x00800000,
    MDER_RESERVED_VALUE = 0x00800001,
    MDER_NEGATIVE_INFINITY = 0x00800002
} ReservedFloatValues;

struct lm_parser {
    const GByteArray *bytes;
    guint offset;
    lm_parser_byte_order_t order;
};

static const double reserved_float_values[5] = {MDER_POSITIVE_INFINITY, MDER_NaN, MDER_NaN, MDER_NaN,
                                                MDER_NEGATIVE_INFINITY};


#define BINARY32_MASK_SIGN 0x80000000
#define BINARY32_MASK_EXPO 0x7FE00000
#define BINARY32_MASK_SNCD 0x007FFFFF
#define BINARY32_IMPLIED_BIT 0x800000
#define BINARY32_SHIFT_EXPO 23

lm_parser_t *lm_parser_create(const GByteArray *bytes, lm_parser_byte_order_t order) {
    lm_parser_t *parser = g_new0(lm_parser_t, 1);
    parser->bytes = bytes;
    parser->offset = 0;
    parser->order = order;
    return parser;
}

void lm_parser_destroy(lm_parser_t *parser) {
    g_assert(parser != NULL);
    parser->bytes = NULL;
    g_free(parser);
}

void lm_parser_set_offset(lm_parser_t *parser, guint offset) {
    g_assert(parser != NULL);
    parser->offset = offset;
}

guint8 lm_parser_get_uint8(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert(parser->offset < parser->bytes->len);

    guint8 result = parser->bytes->data[parser->offset];
    parser->offset = parser->offset + 1;
    return result;
}

gint8 lm_parser_get_sint8(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert(parser->offset < parser->bytes->len);

    gint8 result = (gint8) parser->bytes->data[parser->offset];
    parser->offset = parser->offset + 1;
    return result;
}

guint16 lm_parser_get_uint16(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert((parser->offset + 1) < parser->bytes->len);

    guint8 byte1, byte2;
    byte1 = parser->bytes->data[parser->offset];
    byte2 = parser->bytes->data[parser->offset + 1];
    parser->offset = parser->offset + 2;
    if (parser->order == LM_PARSER_LITTLE_ENDIAN) {
        return (guint16) ((byte2 << 8) + byte1);
    } else {
        return (guint16) ((byte1 << 8) + byte2);
    }
}

gint16 lm_parser_get_sint16(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert((parser->offset + 1) < parser->bytes->len);

    guint8 byte1, byte2;
    byte1 = parser->bytes->data[parser->offset];
    byte2 = parser->bytes->data[parser->offset + 1];
    parser->offset = parser->offset + 2;
    if (parser->order == LM_PARSER_LITTLE_ENDIAN) {
        return (gint16) ((byte2 << 8) + byte1);
    } else {
        return (gint16) ((byte1 << 8) + byte2);
    }
}

guint32 lm_parser_get_uint24(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert((parser->offset + 2) < parser->bytes->len);

    guint8 byte1, byte2, byte3;
    byte1 = parser->bytes->data[parser->offset];
    byte2 = parser->bytes->data[parser->offset+1];
    byte3 = parser->bytes->data[parser->offset+2];
    parser->offset = parser->offset + 3;
    if (parser->order == LM_PARSER_LITTLE_ENDIAN) {
        return (guint32) ((byte3 << 16) + (byte2 << 8) + byte1);
    } else {
        return (guint32) ((byte1 << 16) + (byte2 << 8) + byte3);
    }
}

guint32 lm_parser_get_uint32(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert((parser->offset + 3) < parser->bytes->len);

    guint8 byte1, byte2, byte3, byte4;
    byte1 = parser->bytes->data[parser->offset];
    byte2 = parser->bytes->data[parser->offset + 1];
    byte3 = parser->bytes->data[parser->offset + 2];
    byte4 = parser->bytes->data[parser->offset + 3];
    parser->offset = parser->offset + 4;
    if (parser->order == LM_PARSER_LITTLE_ENDIAN) {
        return (guint32) ((byte4 << 24) + (byte3 << 16) + (byte2 << 8) + byte1);
    } else {
        return (guint32) ((byte1 << 24) + (byte2 << 16) + (byte3 << 8) + byte4);
    }
}

double lm_parser_get_sfloat(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert(parser->offset < parser->bytes->len);

    guint16 sfloat = lm_parser_get_uint16(parser);

    int mantissa = sfloat & 0xfff;
    if (mantissa >= 0x800) {
        mantissa = mantissa - 0x1000;
    }
    int exponent = sfloat >> 12;
    if (exponent >= 0x8) {
        exponent = exponent - 0x10;
    }
    return (mantissa * pow(10.0, exponent));
}

/* round number n to d decimal points */
float fround(float n, int d) {
    int rounded = (int) floor(n * pow(10.0f, d) + 0.5f);
    int divider = (int) pow(10.0f, d);
    return (float) rounded / (float) divider;
}

double lm_parser_get_11073float(lm_parser_t *parser) {
    g_assert(parser != NULL);
    guint32 int_data = lm_parser_get_uint32(parser);

    guint32 mantissa = int_data & 0xFFFFFF;
    gint8 exponent = (gint8) (int_data >> 24);
    double output = 0;

    if (mantissa >= MDER_POSITIVE_INFINITY &&
        mantissa <= MDER_NEGATIVE_INFINITY) {
        output = reserved_float_values[mantissa - MDER_POSITIVE_INFINITY];
    } else {
        if (mantissa >= 0x800000) {
            mantissa = -((0xFFFFFF + 1) - mantissa);
        }
        output = (mantissa * pow(10.0f, exponent));
    }

    return output;
}

double lm_parser_get_754float(lm_parser_t *parser) {
    g_assert(parser != NULL);
    guint32 int_data = lm_parser_get_uint32(parser);

    // Break up into 3 parts
    gboolean sign = (gboolean) (int_data & BINARY32_MASK_SIGN);
    guint32 biased_expo = (int_data & BINARY32_MASK_EXPO) >> BINARY32_SHIFT_EXPO;
    int32_t significand = int_data & BINARY32_MASK_SNCD;

    float result;
    if (biased_expo == 0xFF) {
        result = significand ? NAN : INFINITY;   // For simplicity, NaN payload not copied
    } else {
        guint32 expo;

        if (biased_expo > 0) {
            significand |= BINARY32_IMPLIED_BIT;
            expo = biased_expo - 127;
        } else {
            expo = 126;
        }

        result = ldexpf((float)significand, (int) (expo - BINARY32_SHIFT_EXPO));
    }

    if (sign)
        result = -result;

    return result;
}

double lm_parser_get_754half(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert(parser->offset < parser->bytes->len);

    guint16 value = lm_parser_get_uint16(parser);

    gboolean sign = ((value & 0x8000) != 0);
	guint16 exponent = (value & 0x7c00) >> 10;
	guint16 fraction = value & 0x300;

	float result = 0.0;

	if (exponent == 0) {
		if (fraction == 0) {
			return (0.0);
		} else {
			result = (float) (pow(-1, sign) * pow(2, -14) * ((float) fraction / 1024));
		}
	} else if (exponent == 0x1f) {
		if (fraction == 0)
            return (INFINITY);
		else
            return (NAN);
	} else {
		result = (float) (pow(-1, sign) * pow(2, exponent - 15) * (1.0 + (float) fraction / 1024));
	}

	return (result);
}

GString *lm_parser_get_string(lm_parser_t *parser) {
    g_assert(parser != NULL);
    g_assert(parser->bytes != NULL);

    return g_string_new_len((const char *) parser->bytes->data + parser->offset,
                            (gssize) (parser->bytes->len - parser->offset));
}

GDateTime *lm_parser_get_date_time(lm_parser_t *parser) {
    g_assert(parser != NULL);

    guint16 year = lm_parser_get_uint16(parser);
    guint8 month = lm_parser_get_uint8(parser);
    guint8 day = lm_parser_get_uint8(parser);
    guint8 hour = lm_parser_get_uint8(parser);
    guint8 min = lm_parser_get_uint8(parser);
    guint8 sec = lm_parser_get_uint8(parser);

    return g_date_time_new_local(year, month, day, hour, min, sec);
}
