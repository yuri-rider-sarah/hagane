#include "global.h"

extern "C" void vector_string_error(u64 v_, u64 internal) {
    vector<u64> *v = (vector<u64> *)v_;
    for (u64 c : *v)
        fputc((char)c, stderr);
    fputc('\n', stderr);
    exit(internal ? 2 : 1);
}
