#include "blaze.h"

static int var_counter = 0;

Var* var_new(Func* owner, String* name) {
    Var* res = new(Var);
    res->id = var_counter++;
    res->uses = 0;
    if (name) res->name = string_clone(name);
    return res;
}

void var_dump(Var* v) {
    assert(v);
    printf("Var %d", v->id);
    if (v->name) printf(" (%s)", v->name->str);
}

void var_free(Var* v) {
    if (v->name) string_free(v->name);
    free(v);
}

void instr_dump(Instr* ir) {
    assert(ir);
    switch (ir->kind) {
    case Iret: printf("Iret"); break;
    case Inew: printf("Inew"); break;
    case Iset: printf("Iset"); break;
    case Ivar: printf("Ivar"); break;
    case Iint: printf("Iint (i:%s)", ir->s->str); break;
    }

    if (ir->v) {
        printf(" of ");
        var_dump(ir->v);
    }

    if (ir->dst) {
        printf(" -> ");
        var_dump(ir->dst);
    }

    putchar('\n');
}

void instr_free(Instr* ir) {
    switch (ir->kind) {
    case Iint: string_free(ir->s); break;
    default: break;
    }
    free(ir);
}

void func_dump(Func* f) {
    int i;
    printf("Function %s:\n", f->name->str);
    for (i=0; i<list_len(f->sons); ++i) {
        printf("  ");
        instr_dump(f->sons[i]);
    }
}

void func_free(Func* f) {
    int i;
    for (i=0; i<list_len(f->sons); ++i) instr_free(f->sons[i]);
    for (i=0; i<list_len(f->vars); ++i)
        if (f->vars[i]->owner == f) var_free(f->vars[i]);
}
