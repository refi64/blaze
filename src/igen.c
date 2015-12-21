#include "blaze.h"

static void igen_sons(Func* f, Node* n);
static Var* igen_node(Func* f, Node* n);

static Var* igen_address(Func* f, Node* n) {
    Instr* ir = new(Instr);
    assert(n->flags & Faddr);
    ir->kind = Iaddr;
    list_append(ir->v, igen_node(f, n));
    ir->dst = var_new(f, ir, NULL);
    assert(ir->dst);
    list_append(f->sons, ir);
    return ir->dst;
}

static Var* igen_node(Func* f, Node* n) {
    Instr* ir = new(Instr);
    switch (n->kind) {
    case Nbody:
        igen_sons(f, n);
        instr_free(ir);
        ir = NULL;
        break;
    case Nreturn:
        ir->kind = Iret;
        if (n->sons[0]) list_append(ir->v, igen_node(f, n->sons[0]));
        break;
    case Nlet:
        ir->kind = Inew;
        ir->dst = var_new(f, ir, n->s);
        list_append(ir->v, igen_node(f, n->sons[0]));
        n->v = ir->v[0];
        assert(n->v);
        break;
    case Nassign:
        ir->kind = Iset;
        list_append(ir->v, igen_address(f, n->sons[0]));
        list_append(ir->v, igen_node(f, n->sons[1]));
        ir->dst = ir->v[0];
        break;
    case Nid:
        assert(n->e && n->e->n && n->e->n->v);
        free(ir);
        return n->e->n->v;
    case Nint:
        ir->kind = Iint;
        ir->s = string_clone(n->s);
        ir->dst = var_new(f, ir, NULL);
        break;
    case Nmodule: case Ntypeof: case Nfun: case Narglist: case Narg:
    case Nsons: assert(0);
    }

    if (ir) {
        int i;
        list_append(f->sons, ir);
        for (i=0; i<list_len(ir->v); ++i) ++ir->v[i]->uses;
        return ir->dst;
    } else return NULL;
}

static void igen_sons(Func* f, Node* n) {
    int i;
    assert(n && n->kind > Nsons);
    for (i=0; i<list_len(n->sons); ++i)
        igen_node(f, n->sons[i]);
}

static Func* igen_func(Node* n) {
    assert(n->kind == Nfun);
    Func* res = new(Func);
    res->name = string_clone(n->s);
    igen_node(res, n->sons[2]);
    return res;
}

List(Func*) igen(Node* n) {
    List(Func*) res = NULL;
    int i, j, k;
    assert(n);

    for (i=0; i<list_len(n->sons); ++i) switch (n->sons[i]->kind) {
    case Nmodule:
        for (j=0; j<list_len(n->sons[i]->sons); ++j) {
            List(Func*) t = igen(n->sons[i]->sons[j]);
            for (k=0; k<list_len(t); ++k) list_append(res, t[k]);
        }
        break;
    case Nfun:
        list_append(res, igen_func(n->sons[i]));
        break;
    default: break;
    }

    return res;
}
