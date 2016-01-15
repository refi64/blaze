#include "blaze.h"

static void igen_sons(Decl* d, Node* n);
static Var* igen_node(Decl* d, Node* n);

#define PUREFLAGS(v) ((v)->ir ? (v)->ir->flags & Fpure : Fpure)

static Var* igen_address(Decl* d, Node* n) {
    Instr* ir = new(Instr);
    assert(n->sons[0]->flags & Faddr);
    ir->kind = Iaddr;
    list_append(ir->v, igen_node(d, n->sons[0]));
    ir->flags |= PUREFLAGS(ir->v[0]);
    ir->dst = var_new(d, ir, n->type, NULL);
    list_append(d->sons, ir);
    return ir->dst;
}

static Var* igen_node(Decl* d, Node* n) {
    Instr* ir = new(Instr);
    int i;
    switch (n->kind) {
    case Nbody:
        igen_sons(d, n);
        instr_free(ir);
        ir = NULL;
        break;
    case Nreturn:
        ir->kind = Iret;
        if (n->sons) list_append(ir->v, igen_node(d, n->sons[0]));
        break;
    case Nlet:
        ir->kind = Inew;
        n->v = ir->dst = var_new(d, ir, n->type, n->s);
        list_append(ir->v, igen_node(d, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Nassign:
        ir->kind = Iset;
        list_append(ir->v, igen_address(d, n->sons[0]));
        list_append(ir->v, igen_node(d, n->sons[1]));
        ir->flags |= PUREFLAGS(ir->v[0]) & PUREFLAGS(ir->v[1]);
        break;
    case Nderef:
        ir->kind = Ideref;
        list_append(ir->v, igen_node(d, n->sons[0]));
        ir->dst = var_new(d, ir, n->type, NULL);
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Naddr:
        free(ir);
        return igen_address(d, n);
    case Ncall:
        ir->kind = Icall;
        for (i=0; i<list_len(n->sons); ++i)
            list_append(ir->v, igen_node(d, n->sons[i]));
        ir->dst = var_new(d, ir, n->flags & Fvoid ? NULL : n->type, NULL);
        break;
    case Nid:
        assert(n->e && n->e->n && n->e->n->v);
        free(ir);
        ++n->e->n->v->uses;
        return n->e->n->v;
    case Nint:
        ir->kind = Iint;
        ir->flags |= Fpure;
        ir->s = string_clone(n->s);
        ir->dst = var_new(d, ir, builtin_types[Tint]->override, NULL);
        break;
    case Nmodule: case Ntypeof: case Nfun: case Narglist: case Ndecl:
    case Nsons: case Nptr: assert(0);
    }

    if (ir) {
        int i;
        list_append(d->sons, ir);
        for (i=0; i<list_len(ir->v); ++i) {
            assert(ir->v[i]);
            ++ir->v[i]->uses;
        }
        return ir->dst;
    } else return NULL;
}

static void igen_sons(Decl* d, Node* n) {
    int i;
    assert(n && n->kind > Nsons);
    for (i=0; i<list_len(n->sons); ++i)
        igen_node(d, n->sons[i]);
}

static Decl* igen_func(Module* m, Node* n) {
    int i;
    assert(n->kind == Nfun);
    Decl* res = new(Decl);
    res->kind = Dfun;
    res->name = string_clone(n->s);
    res->m = m;
    assert(n->sons[1]->kind == Narglist);
    res->v = n->v = var_new(res, NULL, n->type, n->s);
    for (i=0; i<list_len(n->sons[1]->sons); ++i) {
        Node* arg = n->sons[1]->sons[i];
        assert(arg->kind == Ndecl);
        Var* v = var_new(res, NULL, arg->type, arg->s);
        list_append(res->args, v);
        arg->v = v;
    }
    if (n->sons[0]) res->ret = n->sons[0]->type;
    if (!n->import) igen_node(res, n->sons[2]);
    else res->import = n->import;
    res->exportc = n->exportc;
    return res;
}

Module* igen(Node* n) {
    Module* res = new(Module);
    int i;
    assert(n && n->kind == Nmodule);

    for (i=0; i<list_len(n->sons); ++i) switch (n->sons[i]->kind) {
    case Nfun:
        list_append(res->decls, igen_func(res, n->sons[i]));
        break;
    default: break;
    }

    return res;
}
