#include "global.h"

extern "C" u64 vector_new() {
    vector<u64> *v = new vector<u64>;
    return (u64)v;
}

extern "C" void vector_push(u64 v_, u64 x) {
    vector<u64> *v = (vector<u64> *)v_;
    v->push_back(x);
}

extern "C" u64 vector_len(u64 v_) {
    vector<u64> *v = (vector<u64> *)v_;
    return (u64)v->size();
}

extern "C" u64 vector_get(u64 v_, u64 i) {
    vector<u64> *v = (vector<u64> *)v_;
    if (i > v->size()) {
        fprintf(stderr, "Index out of range");
        exit(1);
    }
    return (*v)[i];
}
