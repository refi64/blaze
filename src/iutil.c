#include "blaze.h"

static int var_counter = 0;

Var* var_new(Decl* owner, Instr* ir, Type* type, String* name) {
    Var* res = new(Var);
    res->id = var_counter++;
    res->uses = 0;
    res->owner = owner;
    res->ir = ir;
    res->type = type;
    if (ir) list_append(owner->vars, res);
    if (name) res->name = string_clone(name);
    if (type) list_append(owner->m->types, type);
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
    case Inull: break;
    case Iret: printf("Iret"); break;
    case Inew: printf("Inew"); break;
    case Iset: printf("Iset"); break;
    case Ideref: printf("Ideref"); break;
    case Iaddr: printf("Iaddr"); break;
    case Icall: printf("Icall"); break;
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
    printf("Decl %s (", d->name->str);
    var_dump(d->v);
    printf("):\n");
    for (i=0; i<list_len(d->sons); ++i) {
        printf("  ");
        instr_dump(d->sons[i]);
    }
}

void decl_free(Decl* d) {
    int i;
    for (i=0; i<list_len(d->sons); ++i) instr_free(d->sons[i]);
    for (i=0; i<list_len(d->vars); ++i)
        if (d->vars[i]->owner == d && d->vars[i]->ir) var_free(d->vars[i]);
    for (i=0; i<list_len(d->args); ++i) var_free(d->args[i]);
    list_free(d->sons);
    list_free(d->vars);
    list_free(d->args);
    string_free(d->name);
    var_free(d->v);
    free(d);
}

void module_dump(Module* m) {
    int i;
    for (i=0; i<list_len(m->decls); ++i) decl_dump(m->decls[i]);
}

void module_free(Module* m) {
    int i;
    for (i=0; i<list_len(m->decls); ++i) decl_free(m->decls[i]);
    list_free(m->decls);
    list_free(m->types);
    free(m);
}
