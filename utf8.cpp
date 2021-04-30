#include "global.h"

static void utf8_error() {
    fprintf(stderr, "Error: invalid UTF-8 input\n");
    exit(1);
}

static u8 read_byte(vector<u64> *bytes, size_t *i) {
    if (*i >= bytes->size())
        utf8_error();
    return (u8)(*bytes)[(*i)++];
}

static u64 utf8_decode_char(vector<u64> *bytes, size_t *i) {
    u8 b0 = read_byte(bytes, i);
    u64 c = b0;
    if ((b0 & 0x80) == 0x00)
        return c;
    u8 b1 = read_byte(bytes, i);
    if ((b1 & 0xC0) != 0x80)
        utf8_error();
    c = (c << 6) | (b1 & 0x3F);
    if ((b0 & 0xE0) == 0xC0) {
        c &= 0x7FF;
        if (c < 0x80)
            utf8_error();
        return c;
    }
    u8 b2 = read_byte(bytes, i);
    if ((b2 & 0xC0) != 0x80)
        utf8_error();
    c = (c << 6) | (b2 & 0x3F);
    if ((b0 & 0xF0) == 0xE0) {
        c &= 0xFFFF;
        if (c < 0x800 || (0xD800 <= c && c <= 0xDFFF))
            utf8_error();
        return c;
    }
    u8 b3 = read_byte(bytes, i);
    if ((b3 & 0xC0) != 0x80)
        utf8_error();
    c = (c << 6) | (b3 & 0x3F);
    if ((b0 & 0xF8) == 0xF0) {
        c &= 0x1FFFFF;
        if (c < 0x10000 || c > 0x10FFFF)
            utf8_error();
        return c;
    }
    utf8_error();
    return 0;
}

extern "C" u64 utf8_decode(u64 bytes_) {
    vector<u64> *bytes = (vector<u64> *)bytes_;
    size_t i = 0;
    vector<u64> *chars = new vector<u64>;
    while (i < bytes->size())
        chars->push_back(utf8_decode_char(bytes, &i));
    return (u64)chars;
}

extern "C" u64 utf8_encode(u64 chars_) {
    vector<u64> *chars = (vector<u64> *)chars_;
    vector<u64> *bytes = new vector<u64>;
    for (u64 c : *chars) {
        if (c < 0x80) {
            bytes->push_back(c);
        } else if (c < 0x800) {
            bytes->push_back(0xC0 | (c >> 6));
            bytes->push_back(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            bytes->push_back(0xE0 | (c >> 12));
            bytes->push_back(0x80 | ((c >> 6) & 0x3F));
            bytes->push_back(0x80 | (c & 0x3F));
        } else {
            bytes->push_back(0xF0 | (c >> 18));
            bytes->push_back(0x80 | ((c >> 12) & 0x3F));
            bytes->push_back(0x80 | ((c >> 6) & 0x3F));
            bytes->push_back(0x80 | (c & 0x3F));
        }
    }
    return (u64)bytes;
}
