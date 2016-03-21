#include "blaze.h"
#include <unistd.h>

static FILE* open_write(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f)
        fprintf(stderr, "error opening %s for writing: %s\n", path,
                strerror(errno));
    return f;
}

static int write_module(Module* m) {
    FILE* f;

    m->d.cname = string_new(".blaze/");
    string_merge(m->d.cname, m->name);
    string_merges(m->d.cname, ".c");

    f = open_write(m->d.cname->str);
    if (!f) return 0;

    cgen(m, f);
    fclose(f);
    return 1;
}

static void write_compiler_flags(Config config, FILE* f) {
    fputc('F', f);
    if (config.kind == Cclang)
        fputs("-Wno-incompatible-library-redeclaration ", f);
    fputs(config.cflags, f);
    fputc('\n', f);
}

static int write_lightbuild(const char* tgt, Config config, List(Module*) mods) {
    int i;
    FILE* f = open_write(".blaze/build");
    if (!f) return 0;

    fprintf(f, "C%s\n", config.compiler);
    write_compiler_flags(config, f);
    fputs("L\n", f);
    fprintf(f, "T%s\n", tgt);
    fputs("O-o\nX-o\n", f);

    fprintf(f, ":%zu\n", list_len(mods));
    for (i=0; i<list_len(mods); ++i) fprintf(f, "%s\n", mods[i]->d.cname->str);
    fclose(f);
    return 1;
}

void build(const char* tgt, Config config, List(Module*) mods) {
    int i;
    String* s = NULL;
    if (!exists(".blaze") && !pmkdir(".blaze")) return;

    for (i=0; i<list_len(mods); ++i)
        if (!write_module(mods[i])) goto end;

    if (!write_lightbuild(tgt, config, mods)) goto end;

    s = string_new(config.lightbuild);
    string_merges(s, " .blaze/build");
    if (system(s->str)) fputs("C compilation failed!\n", stderr);

end:
    for (i=0; i<list_len(mods); ++i) {
        cgen_free(mods[i]);
        string_free(mods[i]->d.cname);
    }
    cgen_free(NULL);

    if (s) string_free(s);
}
