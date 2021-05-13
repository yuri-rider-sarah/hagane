#include <stdint.h>
#include "char_types_data.h"

extern "C" uint64_t is_ident_character_table_get(uint64_t c) {
    return is_ident_character_table[c];
}
