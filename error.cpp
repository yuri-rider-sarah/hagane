#include "global.h"

extern "C" void vector_string_error(u64 v_) {
    vector<u64> *v = (vector<u64> *)v_;
    for (u64 c : *v)
        fputc((char)c, stderr);
    fputc('\n', stderr);
    exit(1);
}
