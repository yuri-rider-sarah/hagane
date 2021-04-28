#include "global.h"

extern "C" u64 file_contents(u64 filename_) {
    vector<u64> *filename = (vector<u64> *)filename_;
    char *c_filename = new char[filename->size() + 1];
    for (size_t i = 0; i < filename->size(); i++)
        c_filename[i] = (char)(*filename)[i];
    c_filename[filename->size()] = '\0';
    FILE *f = fopen(c_filename, "r");
    if (f == NULL) {
        perror("Failed to open file");
        exit(errno);
    }
    vector<u64> *chars = new vector<u64>;
    for (int c = fgetc(f); c != EOF; c = fgetc(f))
        chars->push_back((u64)c);
    if (!feof(f)) {
        perror("Failed to read file");
        exit(errno);
    }
    fclose(f);
    return (u64)chars;
}
