#include "blaze.h"

static int var_counter = 0;

Var* var_new(Func* owner, Instr* ir, String* name) {
    Var* res = new(Var);
    res->id = var_counter++;
    res->uses = 0;
    res->owner = owner;
    res->ir = ir;
    if (name) res->name = string_clone(name);
    list_append(owner->vars, res);
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
    int i;
    assert(ir);
    switch (ir->kind) {
    case Iret: printf("Iret"); break;
    case Inew: printf("Inew"); break;
    case Iset: printf("Iset"); break;
    case Ivar: printf("Ivar"); break;
    case Iaddr: printf("Iaddr"); break;
    case Iint: printf("Iint (i:%s)", ir->s->str); break;
    }

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
        if (ir->dst->uses > 1) putchar('s');
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
    for (i=0; i<list_len(f->sons); ++i) {
        list_free(f->sons[i]->v);
        instr_free(f->sons[i]);
    }
    for (i=0; i<list_len(f->vars); ++i)
        if (f->vars[i]->owner == f) var_free(f->vars[i]);
    list_free(f->sons);
    list_free(f->vars);
    string_free(f->name);
    free(f);
}
