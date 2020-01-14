/*
 * Bold IBM CGA font
 * Partially adapted to save space and work
 */
#include "mgos.h"

const uint8_t bold_numeric_font[] =
    {
        'F', 1, 0, 88, 8,
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        7, 0x6c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10,       // 0003 (heart)
        7, 0x40, 0x1c, 0x22, 0x20, 0x20, 0x22, 0x1c,       // 0004 (degree celcius)
        7, 0x40, 0x1c, 0x22, 0x0, 0x0, 0x0, 0x0,           // 0005 (stale degree celcius)
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       // 0020 (space)
        7, 0x30, 0x78, 0x78, 0x30, 0x30, 0x00, 0x30,       // 0021 (exclam)
        7, 0x6c, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00,       // 0022 (quotedbl)
        7, 0x6c, 0x6c, 0xfe, 0x6c, 0xfe, 0x6c, 0x6c,       // 0023 (numbersign)
        7, 0x30, 0x7c, 0xc0, 0x78, 0x0c, 0xf8, 0x30,       // 0024 (dollar)
        7, 0x00, 0xc6, 0xcc, 0x18, 0x30, 0x66, 0xc6,       // 0025 (percent)
        7, 0x38, 0x6c, 0x38, 0x76, 0xdc, 0xcc, 0x76,       // 0026 (ampersand)
        7, 0x60, 0x60, 0xc0, 0x00, 0x00, 0x00, 0x00,       // 0027 (quotesingle)
        7, 0x18, 0x30, 0x60, 0x60, 0x60, 0x30, 0x18,       // 0028 (parenleft)
        7, 0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60,       // 0029 (parenright)
        7, 0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00,       // 002a (asterisk)
        7, 0x00, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x00,       // 002b (plus)
        8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x60, // 002c (comma)
        7, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00,       // 002d (hyphen)
        7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30,       // 002e (period)
        7, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80,       // 002f (slash)
        7, 0x7c, 0xc6, 0xce, 0xde, 0xf6, 0xe6, 0x7c,       // 0030 (zero)
        7, 0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xfc,       // 0031 (one)
        7, 0x78, 0xcc, 0x0c, 0x38, 0x60, 0xc4, 0xfc,       // 0032 (two)
        7, 0x78, 0xcc, 0x0c, 0x38, 0x0c, 0xcc, 0x78,       // 0033 (three)
        7, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 0x0c, 0x1e,       // 0034 (four)
        7, 0xfc, 0xc0, 0xf8, 0x0c, 0x0c, 0xcc, 0x78,       // 0035 (five)
        7, 0x38, 0x60, 0xc0, 0xf8, 0xcc, 0xcc, 0x78,       // 0036 (six)
        7, 0xfc, 0xcc, 0x0c, 0x18, 0x30, 0x30, 0x30,       // 0037 (seven)
        7, 0x78, 0xcc, 0xcc, 0x78, 0xcc, 0xcc, 0x78,       // 0038 (eight)
        7, 0x78, 0xcc, 0xcc, 0x7c, 0x0c, 0x18, 0x70,       // 0039 (nine)
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        0,                                                 // null, blank
        7, 0xc6, 0xc6, 0x6c, 0x38, 0x38, 0x6c, 0xc6,       // 0058, (X)
};
