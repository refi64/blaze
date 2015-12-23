#include "blaze.h"

const char* typenames[] = {"int", "char"};
int type_id=0;

static void generate_typename(Type* t) {
    char buf[1024];
    t->d.cname = string_newz("t", 1);
    snprintf(buf, sizeof(buf), "%d", type_id++);
    string_merges(t->d.cname, buf);
    if (t->name) {
        string_mergec(t->d.cname, '_');
        string_merge(t->d.cname, t->name);
    }
}

#define CNAME(t) t->d.cname->str

static void cgen_typedef(Type* t, FILE* output) {
    int i;
    assert(t);
    if (t->d.cname) return;
    generate_typename(t);
    for (i=0; i<list_len(t->sons); ++i)
        if (t->sons[i]) cgen_typedef(t->sons[i], output);
    fprintf(output, "typedef ");
    switch (t->kind) {
    case Tany: assert(0);
    case Tbuiltin:
        fprintf(output, "%s %s", typenames[t->bkind], CNAME(t));
        break;
    case Tptr:
        fprintf(output, "%s* %s", CNAME(t->sons[0]), CNAME(t));
        break;
    case Tfun:
        if (t->sons[0]) fputs(CNAME(t->sons[0]), output);
        else fputs("void", output);
        fprintf(output, " (*%s)(", CNAME(t));
        for (i=1; i<list_len(t->sons); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(t->sons[i]), output);
        }
        fputc(')', output);
        break;
    }
    fputs(";\n", output);
}

static void put_var(FILE* output, Var* v) {
    fprintf(output, "f%d_%s", v->id, v->name ? v->name->str : "");
}

static void cgen_decl(Decl* d, FILE* output) {
    put_var(output, d->v);
}

void cgen(Module* m, FILE* output) {
    int i;
    for (i=0; i<list_len(m->types); ++i)
        cgen_typedef(m->types[i], output);
    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl(m->decls[i], output);
    for (i=0; i<list_len(m->types); ++i)
        string_free(m->types[i]->d.cname);
}
