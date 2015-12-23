#include "blaze.h"

static int var_counter = 0;

Var* var_new(Decl* owner, Instr* ir, Type* type, String* name) {
    Var* res = new(Var);
    res->id = var_counter++;
    res->uses = 0;
    res->owner = owner;
    res->ir = ir;
    res->type = type;
    if (name) res->name = string_clone(name);
    list_append(owner->vars, res);
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
    case Iret: printf("Iret"); break;
    case Inew: printf("Inew"); break;
    case Iset: printf("Iset"); break;
    case Ivar: printf("Ivar"); break;
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
    free(ir);
}

void decl_dump(Decl* f) {
    int i;
    printf("Decl %s (", f->name->str);
    var_dump(f->v);
    printf("):\n");
    for (i=0; i<list_len(f->sons); ++i) {
        printf("  ");
        instr_dump(f->sons[i]);
    }
}

void decl_free(Decl* f) {
    int i;
    for (i=0; i<list_len(f->sons); ++i) {
        list_free(f->sons[i]->v);
        instr_free(f->sons[i]);
    }
    for (i=0; i<list_len(f->vars); ++i)
        if (f->vars[i]->owner == f && f->vars[i]->ir) var_free(f->vars[i]);
    for (i=0; i<list_len(f->args); ++i) var_free(f->args[i]);
    list_free(f->sons);
    list_free(f->vars);
    list_free(f->args);
    string_free(f->name);
    var_free(f->v);
    free(f);
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
