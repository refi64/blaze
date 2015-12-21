#include "blaze.h"

void fatal(const char* msg) {
    fprintf(stderr, "FATAL ERROR: %s\n", msg);
    abort();
}
