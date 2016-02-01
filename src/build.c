#include "blaze.h"

static FILE* open_write(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f)
        fprintf(stderr, "error opening %s for writing: %s\n", path,
                strerror(errno));
    return f;
}

static int write_module(Module* m) {
    FILE* f;
    String* s;

    m->d.cname = string_clone(m->name);
    string_merges(m->d.cname, ".c");
    s = string_new(".blaze/");
    string_merge(s, m->d.cname);

    f = open_write(s->str);
    string_free(s);
    if (!f) return 0;

    cgen(m, f);
    fclose(f);
    return 1;
}

static int write_lightbuild(const char* tgt, Config config, List(Module*) mods) {
    int i;
    FILE* f = open_write(".blaze/build");
    if (!f) return 0;

    fprintf(f, "C%s\n", config.compiler);
    fputs("F\nL\n", f);
    fprintf(f, "T%s\n", tgt);
    fputs("O-o\nX-o\n", f);

    fprintf(f, ":%zu\n", list_len(mods));
    for (i=0; i<list_len(mods); ++i) fprintf(f, "%s\n", mods[i]->d.cname->str);
    fclose(f);
}

void build(const char* tgt, Config config, List(Module*) mods) {
    int i;
    if (!exists(".blaze") && !pmkdir(".blaze")) return;

    for (i=0; i<list_len(mods); ++i)
        if (!write_module(mods[i])) goto end;

    if (!write_lightbuild(tgt, config, mods)) goto end;

end:
    for (i=0; i<list_len(mods); ++i) {
        cgen_free(mods[i]);
        string_free(mods[i]->d.cname);
    }
}
