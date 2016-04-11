#include "blaze.h"

Var* var_new(Decl* owner, Instr* ir, Type* type, String* name) {
    Var* res = new(Var);
    res->uses = 0;
    res->owner = owner;
    res->ir = ir;
    res->type = type;
    if (ir) {
        if (ir == &magic) list_append(owner->mvars, res);
        else list_append(owner->vars, res);
    }
    if (name) res->name = string_clone(name);
    if (type) list_append(owner->m->types, type);
    return res;
}

void var_dump(Var* v) {
    bassert(v, "expected non-null var");
    printf("Var %" PRIdPTR, (uintptr_t)v);
    if (v->deref) {
        printf(" *(");
        var_dump(v->base);
        putchar(')');
    } else if (v->av || v->iv) {
        int i;
        printf(" (");
        var_dump(v->base);
        putchar(')');
        if (v->av)
            for (i=0; i<list_len(v->av); ++i) {
                printf(".(");
                var_dump(*v->av[i]);
                putchar(')');
            }
        else
            for (i=0; i<list_len(v->iv); ++i) {
                printf(" [");
                var_dump(v->iv[i]);
                putchar(']');
            }
    }
    if (v->name) printf(" (%s)", v->name->str);
    if (!v->type) printf(" void");
}

void var_free(Var* v) {
    if (v->name) string_free(v->name);
    list_free(v->av);
    list_free(v->iv);
    free(v);
}

void instr_dump(Instr* ir) {
    int i;
    bassert(ir, "expected non-null ir");
    switch (ir->kind) {
    case Inull: fatal("unexpected ir kind Inull");
    case Iret: printf("Iret"); break;
    case Icjmp: printf("Icjmp (label:%d)", ir->label); break;
    case Ilabel: printf("Ilabel (label:%d)", ir->label); break;
    case Inew: printf("Inew"); break;
    case Iset: printf("Iset"); break;
    case Iaddr: printf("Iaddr"); break;
    case Iconstr: printf("Iconstr"); break;
    case Icall: printf("Icall"); break;
    case Icast: printf("Icast"); break;
    case Iop: printf("Iop (op:%s)", op_strings[ir->op]); break;
    case Iint: printf("Iint (i:%s)", ir->s->str); break;
    }

    if (ir->flags & Fpure)
        printf(" pure");

    if (ir->v) {
        printf(" of (");
        for (i=0; i<list_len(ir->v); ++i) {
            if (i) printf(", ");
            var_dump(ir->v[i]);
        }
        putchar(')');
    }

    if (ir->dst) {
        printf(" -> ");
        var_dump(ir->dst);
        printf(" used %d time", ir->dst->uses);
        if (ir->dst->uses != 1) putchar('s');
    }

    putchar('\n');
}

void instr_free(Instr* ir) {
    switch (ir->kind) {
    case Iint: string_free(ir->s); break;
    default: break;
    }
    list_free(ir->v);
    free(ir);
}

void decl_dump(Decl* d) {
    int i;
    printf("Decl ");
    if (d->name) printf("%s ", d->name->str);
    if (d->v) {
        putchar('(');
        var_dump(d->v);
        printf(") ");
    }
    switch (d->kind) {
    case Dfun:
        printf("function:\n");
        for (i=0; i<list_len(d->sons); ++i) {
            if (d->sons[i]->kind == Inull) continue;
            printf("  ");
            instr_dump(d->sons[i]);
        }
        break;
    case Dglobal:
        printf("global\n");
        break;
    }
}

void decl_free(Decl* d) {
    int i;
    for (i=0; i<list_len(d->sons); ++i) instr_free(d->sons[i]);
    for (i=0; i<list_len(d->vars); ++i)
        if (d->vars[i]->owner == d && d->vars[i]->ir) var_free(d->vars[i]);
    for (i=0; i<list_len(d->mvars); ++i) var_free(d->mvars[i]);
    for (i=0; i<list_len(d->args); ++i) var_free(d->args[i]);
    list_free(d->sons);
    list_free(d->vars);
    list_free(d->mvars);
    list_free(d->args);
    if (d->name) string_free(d->name);
    if (d->v) var_free(d->v);
    if (d->rv) var_free(d->rv);
    free(d);
}

void module_dump(Module* m) {
    int i;
    for (i=0; i<list_len(m->decls); ++i) decl_dump(m->decls[i]);
}

void module_free(Module* m) {
    int i;
    for (i=0; i<list_len(m->decls); ++i)
        if (!(m->decls[i]->flags & Fmemb)) decl_free(m->decls[i]);
    list_free(m->decls);
    for (i=0; i<list_len(m->types); ++i) {
        int j;
        Type* t = m->types[i];
        for (j=0; j<list_len(t->d.sons); ++j) decl_free(t->d.sons[j]);
        list_free(t->d.sons);
        t->d.sons = NULL;
    }
    list_free(m->types);
    list_free(m->imports);
    string_free(m->name);
    free(m);
}
