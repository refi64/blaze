/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"
#include <unistd.h>
#include <sys/stat.h>

#define BZ 1024

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


uint32_t strhash(String* str) { return ds_strhash(str->str); }
int streq(String* a, String* b) { return ds_streq(a->str, b->str); }

int exists(const char* path) { return access(path, F_OK) != -1; }

int pmkdir(const char* dir) {
    if (mkdir(dir, 0775) == -1) {
        fprintf(stderr, "error creating %s: %s\n", dir, strerror(errno));
        return 0;
    } else return 1;
}
