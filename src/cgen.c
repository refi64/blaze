#include "blaze.h"

const char* typenames[] = {"int", "char"};
int type_id=0;

#define CNAME(x) ((x)?(x)->d.cname->str:"void")

static void generate_basename(char p, GData* d, String* name, int id) {
    char buf[1024];
    if (d->cname) return;
    d->cname = string_newz(&p, 1);
    snprintf(buf, sizeof(buf), "%d", id);
    string_merges(d->cname, buf);
    if (name) {
        string_mergec(d->cname, '_');
        string_merge(d->cname, name);
    }
}

static void generate_typename(Type* t) {
    if (t->kind == Tbuiltin) t->d.cname = string_new(typenames[t->bkind]);
    else generate_basename('t', &t->d, t->name, type_id++);
}

static void generate_argname(Var* v) {
    generate_basename('a', &v->d, v->name, v->id);
}

static void generate_varname(Var* v) {
    if (v->deref) {
        String* s = string_newz("(*", 2);
        assert(v->base);
        string_merge(s, v->base->d.cname);
        string_mergec(s, ')');
        v->d.cname = s;
    } else {
        if (v->base) {
            generate_varname(v->base);
            v->d.cname = string_clone(v->base->d.cname);
        } else generate_basename('v', &v->d, v->name, v->id);
        if (v->av) {
            Var* last = *v->av[list_len(v->av)-1];
            if (last->flags & Fstc) {
                string_free(v->d.cname);
                v->d.cname = string_clone(last->d.cname);
                v->flags |= Fstc;
            } else {
                int i;
                for (i=0; i<list_len(v->av); ++i) {
                    string_mergec(v->d.cname, '.');
                    string_merge(v->d.cname, (*v->av[i])->d.cname);
                }
            }
        }
    }
}

static void generate_declname(Decl* d) {
    static const char prefixes[] = "fg";

    if (d->import) d->v->d.cname = string_clone(d->import);
    else if (d->exportc) d->v->d.cname = string_clone(d->exportc);
    else generate_basename(prefixes[d->kind], &d->v->d, d->v->name, d->v->id);
}

static void cgen_typedef(Type* t, FILE* output) {
    int i;
    assert(t);
    if (t->d.cname) return;
    generate_typename(t);
    for (i=0; i<list_len(t->sons); ++i)
        if (t->sons[i]) cgen_typedef(t->sons[i], output);
    switch (t->kind) {
    case Tany: assert(0);
    case Tbuiltin: break;
    case Tptr:
        fprintf(output, "typedef %s* %s;\n", CNAME(t->sons[0]), CNAME(t));
        break;
    case Tfun:
        fprintf(output, "typedef %s", CNAME(t->sons[0]));
        fprintf(output, " (*%s)(", CNAME(t));
        for (i=1; i<list_len(t->sons); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(t->sons[i]), output);
        }
        fputs(");\n", output);
        break;
    case Tstruct:
        fprintf(output, "typedef struct %s %s;\n", CNAME(t), CNAME(t));
        break;
    }
}

static void cgen_decl0(Decl* d, FILE* output);

static void cgen_typeimpl(Type* t, FILE* output) {
    int i;

    if (t->kind != Tstruct || t->d.done) return;
    fprintf(output, "struct %s {\n", CNAME(t));
    for (i=0; i<list_len(t->d.sons); ++i) {
        Decl* d = t->d.sons[i];
        if (d->kind != Dglobal) continue;
        fputs("    ", output);
        cgen_decl0(d, output);
    }
    fputs("};\n", output);
    t->d.done = 1;
}

static void cgen_ir(Instr* ir, FILE* output) {
    int i;
    for (i=0; i<list_len(ir->v); ++i) generate_varname(ir->v[i]);
    // The IR was optimized out by either iopt or cgen_decl1.
    if (ir->kind == Inull || (ir->kind == Iaddr && ir->dst->uses == 0)) return;

    fputs("    ", output);
    if (ir->dst && ir->dst->type) fprintf(output, "%s = ", CNAME(ir->dst));

    switch (ir->kind) {
    case Inull: assert(0);
    case Iret:
        fputs("return", output);
        if (ir->v) fprintf(output, " %s", CNAME(ir->v[0]));
        break;
    case Iset:
        if (ir->v[0]->ir->kind == Iaddr) fputs(CNAME(ir->v[0]->ir->v[0]), output);
        else fprintf(output, "*%s", CNAME(ir->v[0]));
        fprintf(output, " = %s", CNAME(ir->v[1]));
        break;
    case Inew:
        fputs(CNAME(ir->v[0]), output);
        break;
    case Icall:
        fprintf(output, "%s(", CNAME(ir->v[0]));
        if (ir->v[0]->flags & Fstc && ir->v[0]->base) {
            fprintf(output, "&(%s", CNAME(ir->v[0]->base));
            for (i=0; i<list_len(ir->v[0]->av)-1; ++i)
                fprintf(output, ".%s", CNAME(*ir->v[0]->av[i]));
            fputc(')', output);
            if (list_len(ir->v) > 1) fputs(", ", output);
        }
        for (i=1; i<list_len(ir->v); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(ir->v[i]), output);
        }
        fputc(')', output);
        break;
    case Iaddr:
        if (ir->v[0]->deref) fputs(CNAME(ir->v[0]->base), output);
        else fprintf(output, "&%s", CNAME(ir->v[0]));
        break;
    case Iint:
        fputs(ir->s->str, output);
        break;
    }
    fputs(";\n", output);
}

static void cgen_proto(Decl* d, FILE* output) {
    int i;

    assert(d->kind == Dfun);
    if (!d->exportc && !d->import) fputs("static ", output);
    fprintf(output, "%s %s(", CNAME(d->v->type->sons[0]), CNAME(d->v));
    for (i=0; i<list_len(d->args); ++i) {
        generate_argname(d->args[i]);
        if (i) fputs(", ", output);
        fprintf(output, "%s %s", CNAME(d->args[i]->type), CNAME(d->args[i]));
    }
    fputc(')', output);
}

static void cgen_decl0(Decl* d, FILE* output) {
    generate_declname(d);
    switch (d->kind) {
    case Dfun:
        cgen_proto(d, output);
        fputs(";\n", output);
        break;
    case Dglobal:
        fprintf(output, "%s %s;\n", CNAME(d->v->type), CNAME(d->v));
        break;
    }

}

static void cgen_decl1(Decl* d, FILE* output) {
    int i;
    if (d->kind != Dfun || d->import) return;
    cgen_proto(d, output);
    fputs(" {\n", output);
    for (i=0; i<list_len(d->vars); ++i) {
        Var* v = d->vars[i];
        if (!v->type) continue;
        generate_varname(v);
        if (v->assign && v->uses == 1) --v->uses;
        else fprintf(output, "    %s %s;\n", CNAME(v->type), CNAME(v));
    }
    for (i=0; i<list_len(d->sons); ++i) cgen_ir(d->sons[i], output);
    fputs("}\n\n", output);
}

#define FREE_CNAME(b) do {\
        String** p = &(b)->d.cname;\
        if (*p) string_free(*p);\
        *p = NULL;\
    } while (0)

static void free_type_cnames(Type* t) {
    int i;
    if (!t) return;
    FREE_CNAME(t);
    for (i=0; i<list_len(t->sons); ++i) free_type_cnames(t->sons[i]);
}

void cgen(Module* m, FILE* output) {
    int i;
    for (i=0; i<list_len(m->types); ++i)
        cgen_typedef(m->types[i], output);
    fputs("\n\n", output);

    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl0(m->decls[i], output);
    fputs("\n\n", output);

    for (i=0; i<list_len(m->types); ++i)
        cgen_typeimpl(m->types[i], output);
    fputs("\n\n", output);

    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl1(m->decls[i], output);

    for (i=0; i<list_len(m->types); ++i) free_type_cnames(m->types[i]);
    for (i=0; i<list_len(m->decls); ++i) {
        int j;
        Decl* d = m->decls[i];
        string_free(d->v->d.cname);
        for (j=0; j<list_len(d->args); ++j) FREE_CNAME(d->args[j]);
        for (j=0; j<list_len(d->vars); ++j) FREE_CNAME(d->vars[j]);
    }
}

#undef FREE_CNAME
