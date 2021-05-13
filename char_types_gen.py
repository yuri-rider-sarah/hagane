#!/usr/bin/env python3

import unicodedata

permitted_punctuation = [
    '%', '&', '*', '/', '\\',
    '؉', '؊', '٪', '‰', '‱',
    '′', '″', '‴', '‵', '‶', '‷',
    '⁂', '⁗', '⁎', '⁑', '⁕',
    '﹠', '﹡', '﹨', '﹪',
    '％', '＆', '＊', '／', '＼',
]

inner_ident_cats = ['Mn', 'Mc', 'Me', 'Nd', 'Nl', 'No']
starting_ident_cats = ['Lu', 'Ll', 'Lt', 'Lm', 'Lo', 'Pc', 'Pd', 'Sm', 'Sc', 'Sk', 'So']

f = open('char_types_data.h', 'w')

f.write('uint8_t is_ident_character_table[] = {')
for c in range(0x110000):
    cat = unicodedata.category(chr(c))
    if cat in starting_ident_cats or chr(c) in permitted_punctuation:
        f.write('2,')
    elif cat in inner_ident_cats:
        f.write('1,')
    else:
        f.write('0,')
f.write('};\n')
