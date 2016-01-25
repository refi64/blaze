#include "blaze.h"

#define BZ 1024

void fatal(const char* msg) {
    fprintf(stderr, "FATAL ERROR: %s\n", msg);
    abort();
}

int min(int a, int b) { return a < b ? a : b; }

char* readall(FILE* fp, size_t* sz) {
    char* res = alloc(BZ), *p = res;
    *sz = 0;
    for (;;) {
        size_t read = fread(p, 1, BZ, fp);
        *sz += read;
        if (feof(fp)) break;
        else if (ferror(fp)) {
            free(res);
            *sz = 0;
            return NULL;
        }
        res = ralloc(res, *sz+BZ);
        p = res+*sz;
    }

    res = ralloc(res, *sz+1);
    res[*sz] = 0;
    return res;
}
