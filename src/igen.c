#include "blaze.h"

static void igen_sons(Decl* d, Node* n);
static Var* igen_node(Decl* d, Node* n);

#define PUREFLAGS(v) ((v)->ir ? (v)->ir->flags & Fpure : Fpure)

static Decl* igen_decl(Module* m, Node* n);

static void igen_struct(Module* m, Node* n) {
    int i;

    assert(n->kind == Nstruct);
    for (i=0; i<list_len(n->sons); ++i) {
        Decl* d = igen_decl(m, n->sons[i]);
        if (d) {
            d->flags |= Fmemb;
            if (n->sons[i]->kind == Nconstr || n->sons[i]->kind == Nfun)
                d->v->flags |= Fstc;
            list_append(n->type->d.sons, d);
            if (d->kind == Dfun) list_append(m->decls, d);
        }
    }
}

static Var* instr_result(Decl* d, Instr* ir) {
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

static Var* igen_address(Decl* d, Node* n) {
    Instr* ir = new(Instr);
    assert(n->sons[0]->flags & Faddr);
    ir->kind = Iaddr;
    list_append(ir->v, igen_node(d, n->sons[0]));
    ir->flags |= PUREFLAGS(ir->v[0]);
    ir->dst = var_new(d, ir, n->type, NULL);
    return instr_result(d, ir);
}

static Var* igen_attr_chain(Decl* d, Var* v, Node* n) {
    // Make sure the original attributes come FIRST.
    if (n->kind != Nattr) {
        v->base = igen_node(d, n);
        ++v->base->uses;
    } else {
        igen_attr_chain(d, v, n->sons[0]);
        list_append(v->av, &n->attr->d->v);
    }
}

static Var* igen_node(Decl* d, Node* n) {
    Instr* ir = new(Instr);
    Var* v;
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
        list_append(ir->v, igen_address(d, n));
        ir->v[0]->assign = 1;
        list_append(ir->v, igen_node(d, n->sons[1]));
        ir->flags |= PUREFLAGS(ir->v[0]) & PUREFLAGS(ir->v[1]);
        break;
    case Nderef:
        free(ir);
        v = var_new(d, NULL, n->type, NULL);
        v->deref = 1;
        v->base = igen_node(d, n->sons[0]);
        ++v->base->uses;
        return v;
    case Naddr:
        free(ir);
        return igen_address(d, n);
    case Nindex:
        break;
    case Nnew: case Ncall:
        ir->kind = n->kind == Nnew ? Iconstr : Icall;
        ir->dst = var_new(d, ir, n->flags & Fvoid ? NULL : n->type, NULL);
        for (i=0; i<list_len(n->sons); ++i)
            list_append(ir->v, igen_node(d, n->sons[i]));
        break;
    case Ncast:
        ir->kind = Icast;
        ir->dst = var_new(d, ir, n->type, NULL);
        list_append(ir->v, igen_node(d, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Nattr:
        free(ir);
        v = var_new(d, NULL, n->type, NULL);
        igen_attr_chain(d, v, n);
        return v;
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
    case Nmodule: case Ntypeof: case Nstruct: case Nconstr: case Nfun:
    case Narglist: case Ndecl: case Nsons: case Nptr: assert(0);
    }

    return instr_result(d, ir);
}

static void igen_sons(Decl* d, Node* n) {
    int i;
    assert(n && n->kind > Nsons);
    for (i=0; i<list_len(n->sons); ++i)
        igen_node(d, n->sons[i]);
}

static void igen_func(Module* m, Decl* d, Node* n) {
    int i;

    assert(n->kind == Nconstr || n->kind == Nfun);
    d->kind = Dfun;
    assert(n->sons[1]->kind == Narglist);
    d->v = n->v = var_new(d, NULL, n->type, n->s);
    if (n->kind == Nconstr) n->parent->v = n->v;

    if (n->flags & Fmemb) {
        assert(n->this);
        n->this->v = var_new(d, NULL, n->this->type, NULL);
        n->this->v->uses = 1;
        /* if (n->kind == Nconstr) list_append(d->vars, n->this->v); */
        /* else { */
            Var* v;

            Type* orig = n->this->type;
            n->this->type = new(Type);
            n->this->type->kind = Tptr;
            list_append(n->this->type->sons, orig);
            type_incref(n->this->type);
            n->this->v->type = n->this->type;
            list_append(d->m->types, n->this->type);
            list_append(d->args, n->this->v);

            v = var_new(d, NULL, n->this->type, NULL);
            v->base = n->this->v;
            v->deref = 1;
            n->this->v = v;
        /* } */
    }

    for (i=0; i<list_len(n->sons[1]->sons); ++i) {
        Node* arg = n->sons[1]->sons[i];
        assert(arg->kind == Ndecl);
        Var* v = var_new(d, NULL, arg->type, arg->s);
        list_append(d->args, v);
        arg->v = v;
    }

    if (n->sons[0]) d->ret = n->sons[0]->type;
    if (!n->import) igen_node(d, n->sons[2]);
    else d->import = n->import;

    d->exportc = n->exportc;
}

static void igen_global(Module* m, Decl* d, Node* n) {
    assert(n->kind == Ndecl);
    d->kind = Dglobal;
    d->v = n->v = var_new(d, NULL, n->type, n->s);
}

static Decl* igen_decl(Module* m, Node* n) {
    Decl* d = new(Decl);
    if (n->s) d->name = string_clone(n->s);
    d->m = m;

    switch (n->kind) {
    case Nconstr: case Nfun:
        igen_func(m, d, n);
        break;
    case Ndecl:
        igen_global(m, d, n);
        break;
    case Nstruct: default:
        if (d->name) string_free(d->name);
        free(d);
        return NULL;
    }

    n->d = d;
    return d;
}

Module* igen(Node* n) {
    Module* res = new(Module);
    int i;
    assert(n && n->kind == Nmodule);

    for (i=0; i<list_len(n->sons); ++i) {
        Node* ns = n->sons[i];
        if (ns->kind == Nstruct) igen_struct(res, ns);
        else {
            Decl* d = igen_decl(res, n->sons[i]);
            if (d) list_append(res->decls, d);
        }
    }

    return res;
}
