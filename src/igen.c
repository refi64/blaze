#include "blaze.h"

Instr magic;

static void igen_sons(Decl* d, List(Instr*)* tgt, Node* n);
static Var* igen_node(Decl* d, List(Instr*)* tgt, Node* n);

#define PUREFLAGS(v) ((v)->ir ? (v)->ir->flags & Fpure : Fpure)

static Decl* igen_decl(Module* m, Node* n);

static void igen_struct(Module* m, Node* n) {
    int i;

    bassert(n->kind == Nstruct, "unexpected node kind %d", n->kind);
    for (i=0; i<list_len(n->sons); ++i) {
        Decl* d = igen_decl(m, n->sons[i]);
        if (d) {
            d->flags |= Fmemb;
            if (n->sons[i]->kind == Nfun) d->v->flags |= Fstc;
            list_append(n->type->d.sons, d);
            if (d->kind == Dfun) list_append(m->decls, d);
            if (n->export) d->export = 1;
        }
    }
}

static Var* instr_result(Decl* d, List(Instr*)* tgt, Instr* ir) {
    if (ir) {
        int i;
        list_append(*tgt, ir);
        for (i=0; i<list_len(ir->v); ++i) {
            bassert(ir->v[i], "null variable at index %d", i);
            ++ir->v[i]->uses;
        }
        return ir->dst;
    } else return NULL;
}

static Var* igen_address(Decl* d, List(Instr*)* tgt, Node* n) {
    Instr* ir = new(Instr);
    bassert(n->sons[0]->flags & Faddr, "expected addressable node");
    ir->kind = Iaddr;
    list_append(ir->v, igen_node(d, tgt, n->sons[0]));
    ir->flags |= PUREFLAGS(ir->v[0]);
    ir->dst = var_new(d, ir, n->type, NULL);
    return instr_result(d, tgt, ir);
}

static void igen_attr_chain(Decl* d, List(Instr*)* tgt, Var* v, Node* n) {
    // Make sure the original attributes come FIRST.
    if (n->kind != Nattr) {
        v->base = igen_node(d, tgt, n);
        ++v->base->uses;
    } else {
        igen_attr_chain(d, tgt, v, n->sons[0]);
        list_append(v->av, &n->attr->d->v);
    }
}

static void igen_index_chain(Decl* d, List(Instr*)* tgt, Var* v, Node* n) {
    if (n->kind != Nindex) {
        v->base = igen_node(d, tgt, n);
        ++v->base->uses;
    } else {
        igen_index_chain(d, tgt, v, n->sons[0]);
        list_append(v->iv, igen_node(d, tgt, n->sons[1]));
        ++v->iv[list_len(v->iv)-1]->uses;
    }
}

static void igen_destr(Decl* d, List(Instr*)* tgt, Var* v) {
    Instr* ir;
    STEntry* destr;
    if (v->dgen || !v->type || v->type->kind != Tstruct) return;
    destr = v->type->n->magic[Mdelete];
    if (!destr) return;
    ir = new(Instr);
    ir->kind = Idel;
    list_append(ir->v, destr->overloads[0]->n->v);
    list_append(ir->v, v);
    v->dgen = 1;
    instr_result(d, tgt, ir);
}

static Var* igen_node(Decl* d, List(Instr*)* tgt, Node* n) {
    Instr* ir = new(Instr);
    Var* v;
    int i, j, label;
    switch (n->kind) {
    case Nbody:
        igen_sons(d, tgt, n);
        instr_free(ir);
        ir = NULL;
        break;
    case Nreturn:
        ir->kind = Iret;
        if (n->sons) list_append(ir->v, igen_node(d, tgt, n->sons[0]));
        break;
    case Nif:
        ir->kind = Icjmp;
        list_append(ir->v, igen_node(d, tgt, n->sons[0]));
        ir->label = label = d->labels++;
        instr_result(d, tgt, ir); // XXX
        j = list_len(d->vars);
        for (i=1; i<list_len(n->sons); ++i) igen_node(d, tgt, n->sons[i]);
        for (; j<list_len(d->vars); ++j) igen_destr(d, tgt, d->vars[j]);

        ir = new(Instr);
        ir->kind = Ilabel;
        ir->label = label;
        break;
    case Nlet:
        ir->kind = Inew;
        n->v = ir->dst = var_new(d, ir, n->type, n->s);
        list_append(ir->v, igen_node(d, tgt, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Nassign:
        ir->kind = Iset;
        list_append(ir->v, igen_address(d, tgt, n));
        ir->v[0]->assign = 1;
        list_append(ir->v, igen_node(d, tgt, n->sons[1]));
        ir->flags |= PUREFLAGS(ir->v[0]) & PUREFLAGS(ir->v[1]);
        break;
    case Nderef:
        free(ir);
        v = var_new(d, &magic, n->type, NULL);
        v->deref = 1;
        v->base = igen_node(d, tgt, n->sons[0]);
        ++v->base->uses;
        return v;
    case Naddr:
        free(ir);
        return igen_address(d, tgt, n);
    case Nindex:
        free(ir);
        v = var_new(d, &magic, n->type, NULL);
        igen_index_chain(d, tgt, v, n);
        return v;
    case Nnew: case Ncall:
        ir->kind = n->kind == Nnew ? Iconstr : Icall;
        ir->dst = var_new(d, ir, n->flags & Fvoid ? NULL : n->type, NULL);
        for (i=0; i<list_len(n->sons); ++i)
            list_append(ir->v, igen_node(d, tgt, n->sons[i]));
        break;
    case Ncast:
        ir->kind = Icast;
        ir->dst = var_new(d, ir, n->type, NULL);
        list_append(ir->v, igen_node(d, tgt, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Nattr:
        free(ir);
        v = var_new(d, &magic, n->type, NULL);
        igen_attr_chain(d, tgt, v, n);
        return v;
    case Nop:
        ir->kind = Iop;
        ir->dst = var_new(d, ir, n->type, NULL);
        ir->op = n->op;
        list_append(ir->v, igen_node(d, tgt, n->sons[0]));
        list_append(ir->v, igen_node(d, tgt, n->sons[1]));
        break;
    case Nid:
        bassert(n->e && n->e->n && n->e->n->v,
                "node of kind Nid has no corresponding entry");
        free(ir);
        ++n->e->n->v->uses;
        return n->e->n->v;
    case Nint: case Nstr:
        ir->kind = n->kind == Nint ? Iint : Istr;
        ir->flags |= Fpure;
        ir->s = string_clone(n->s);
        ir->dst = var_new(d, ir, n->type, NULL);
        break;
    case Nmodule: case Ntypeof: case Nstruct: case Nfun: case Narglist:
    case Ndecl: case Nsons: case Nptr:
        fatal("unexpected node kind %d", n->kind);
    }

    return instr_result(d, tgt, ir);
}

static void igen_sons(Decl* d, List(Instr*)* tgt, Node* n) {
    int i;
    bassert(n && n->kind > Nsons, "unexpected son-less node kind %d",
            n?n->kind:-1);
    for (i=0; i<list_len(n->sons); ++i)
        igen_node(d, tgt, n->sons[i]);
}

static void igen_func(Module* m, Decl* d, Node* n) {
    int i;

    bassert(n->kind == Nfun, "unexpected node kind %d", n->kind);
    d->kind = Dfun;
    d->v = n->v = var_new(d, NULL, n->type, n->s);

    if (n->flags & Fmemb) {
        Var* v;
        Type* orig;
        bassert(n->this, "member without corresponding this");
        n->this->v = var_new(d, NULL, n->this->type, NULL);
        n->this->v->uses = 1;

        orig = n->this->type;
        n->this->type = new(Type);
        n->this->type->kind = Tptr;
        list_append(n->this->type->sons, orig);
        type_incref(n->this->type);
        n->this->v->type = n->this->type;
        list_append(d->m->types, n->this->type);
        list_append(d->args, n->this->v);

        v = var_new(d, &magic, n->this->type, NULL);
        v->base = n->this->v;
        v->deref = 1;
        n->this->v = v;
    }

    if (n->sons[1]) {
        bassert(n->sons[1]->kind == Narglist, "unexpected node kind %d",
                n->sons[1]->kind);
        for (i=0; i<list_len(n->sons[1]->sons); ++i) {
            Node* arg = n->sons[1]->sons[i];
            bassert(arg->kind == Ndecl, "unexpected node kind %d", arg->kind);
            Var* v = var_new(d, NULL, arg->type, arg->s);
            v->flags |= Farg;
            list_append(d->args, v);
            arg->v = v;
        }
    }

    if (n->sons[0]) {
        d->ret = n->sons[0]->type;
        d->rv = var_new(d, NULL, n->sons[0]->type, NULL);
        d->ra = !(d->ret->kind == Tbuiltin || d->ret->kind == Tptr);
        /* XXX: Even if d->ra is true, the type if d->rv is still not a pointer,
                which is basically lying to the code generator. */
    }

    if (!n->import) {
        Instr* ir = new(Instr);
        igen_node(d, &d->sons, n->sons[2]);
        d->rl = d->labels++;
        ir->kind = Ilabel;
        ir->label = d->rl;
        instr_result(d, &d->sons, ir);
        for (i=0; i<list_len(d->vars); ++i) igen_destr(d, &d->sons, d->vars[i]);
    } else d->import = n->import;

    d->exportc = n->exportc;
}

static void igen_global(Module* m, Decl* d, Node* n) {
    bassert(n->kind == Ndecl, "unexpected node kind %d", n->kind);
    d->kind = Dglobal;
    d->v = n->v = var_new(d, NULL, n->type, n->s);
    ++d->v->uses;

    if (n->sons[1]) {
        Instr* set = new(Instr), *addr = new(Instr);

        addr->kind = Iaddr;
        list_append(addr->v, d->v);
        // XXX: cgen optimizations must remove this variable to avoid void errors!
        addr->dst = var_new(m->init, addr, NULL, NULL);
        list_append(m->init->sons, addr);

        set->kind = Iset;
        list_append(set->v, addr->dst);
        list_append(set->v, igen_node(m->init, &m->init->sons, n->sons[1]));
        ++set->v[1]->uses;

        list_append(m->init->sons, set);
    } else if (n->import) d->import = n->import;
}

static Decl* igen_decl(Module* m, Node* n) {
    Decl* d = new(Decl);
    if (n->s) d->name = string_clone(n->s);
    if (n->export) d->export = 1;
    d->m = m;

    switch (n->kind) {
    case Nfun:
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
    Module* res;
    int i;
    bassert(n && n->kind == Nmodule, "unexpected node kind %d", n?n->kind:-1);

    memset(&magic, 0, sizeof(magic));

    res = n->m = new(Module);
    if (n != builtins_module) list_append(res->imports, builtins_module->m);

    res->name = string_clone(n->s);

    res->init = new(Decl);
    res->init->kind = Dfun;
    // Initializers are hidden, so they have no type.
    res->init->v = var_new(res->init, NULL, NULL, NULL);
    res->init->m = res;
    res->init->export = 1;

    list_append(res->decls, res->init);

    for (i=0; i<list_len(n->sons); ++i) {
        Node* ns = n->sons[i];
        if (ns->kind == Nstruct) igen_struct(res, ns);
        else {
            Decl* d = igen_decl(res, n->sons[i]);
            if (d) {
                list_append(res->decls, d);
                if (IS_MAIN(n->sons[i])) {
                    bassert(!res->main, "duplicate mains in module");
                    res->main = d;
                }
            }
        }
    }

    return res;
}
