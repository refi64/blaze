#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

typedef struct File File;
typedef struct Options Options;

struct File {
    char* path;
    int dirty;
};

struct Options {
    char* compiler, *cflags, *lflags;
};

static void fatal(const char* msg) {
    fprintf(stderr, "fatal error: %s\n", msg);
    exit(EXIT_SUCCESS);
}

static void* alloc(size_t sz) {
    void* res = calloc(sz, 1);
    if (!res) fatal("out of memory");
    return res;
}

typedef void* (*spawn_func)(void* ptr);

#ifdef HAVE_PTHREAD_H
#include <pthread.h>

typedef pthread_t Thread;

static void spawn(spawn_func f, Thread* t, void* arg) {
    int err;
    if ((err = pthread_create(t, NULL, f, arg)))
        fatal(strerror(err));
}

static void stop(Thread t) {
    void* ret;
    assert(pthread_cancel(t) == 0);
    assert(pthread_join(t, &ret) == 0);
    assert(ret == PTHREAD_CANCELED);
}

static void thread_setup() {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
}

#define MAX_PROCESSING 10
#else
#error TODO
#define MAX_PROCESSING 1
#endif

Thread threads[MAX_PROCESSING];
File* all_files;
int nfiles;
File* processing[MAX_PROCESSING];

#ifdef __GNUC__

#ifdef __ATOMIC_SEQ_CST
#define atomic_load(x) __atomic_load_n(x, __ATOMIC_SEQ_CST)
#define atomic_store(x,v) __atomic_store_n(x, v, __ATOMIC_SEQ_CST)
#define queue_test(n,f) do {\
    File* expected = NULL;\
    if (__atomic_compare_exchange_n(&processing[n], &expected, f, 0,\
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;\
} while (0)
#else
#error TODO
#endif

#else

#error TODO

#endif

static void queue_file(File* f) {
    int i;
    for (;;) for (i=0; i<MAX_PROCESSING; ++i) queue_test(i, f);
}

static void wait_all() {
    int i, cont;
    for (;;) {
        cont = 0;
        for (i=0; i<MAX_PROCESSING; ++i)
            if (atomic_load(&processing[i])) {
                cont = 1;
                break;
            }
        if (!cont)
            for (i=0; i<MAX_PROCESSING; ++i) stop(threads[i]);
    }
}

static Options opts;

static void parse(const char* path) {
    char buf[128];
    int i=0;
    FILE* f;
    memset(&opts, 0, sizeof(Options));

    if (!(f = fopen(path, "r"))) fatal(strerror(errno));

    while (fgets(buf, sizeof(buf), f)) {
        size_t bufsz = strlen(buf);
        if (buf[bufsz-1] == '\n') buf[bufsz--] = 0;
        if (bufsz == 0) continue;
        if (all_files) {
            all_files[i].path = alloc(bufsz+1);
            memcpy(all_files[i].path, buf, bufsz+1);
            ++i;
        } else switch (buf[0]) {
        #define C(c,t)\
        case c:\
            opts.t = alloc(bufsz+1);\
            memcpy(opts.t, buf, bufsz+1);\
            break;
        C('C', compiler)
        C('F', cflags)
        C('L', lflags)
        #undef C
        case ':':
            nfiles = atoi(buf+1);
            assert(nfiles);
            all_files = alloc(nfiles*sizeof(File));
            break;
        default:
            fprintf(stderr, "warning: unrecognized line opener %c\n", buf[0]);
            break;
        }
    }

    if (!feof(f)) fatal(strerror(errno));

    assert(all_files);
    assert(opts.compiler);
    assert(opts.cflags);
    assert(opts.lflags);
}

static void* dirty_thread(void* fv) {
    File** ptr = fv;
    File* f = NULL;
    thread_setup();
    for (;;) {
        while (!(f = atomic_load(ptr)));
        f->dirty = 1;
        atomic_store(ptr, NULL);
    }
}

static void dirty_tests() {
    int i;
    for (i=0; i<MAX_PROCESSING; ++i)
        spawn(dirty_thread, &threads[i], &processing[i]);
    for (i=0; i<nfiles; ++i) queue_file(&all_files[i]);
    wait_all();

    for (i=0; i<nfiles; ++i)
        printf("%s: %d\n", all_files[i].path, all_files[i].dirty);
}

int main(int argc, char** argv) {
    if (argc != 2) fatal("usage: lightbuild <build script>");
    parse(argv[1]);
    dirty_tests();
    free(all_files);
}
