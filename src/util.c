#include "blaze.h"

void fatal(const char* msg) {
    fprintf(stderr, "FATAL ERROR: %s\n", msg);
    abort();
}

int min(int a, int b) { return a < b ? a : b; }
