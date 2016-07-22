/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

Instr magic;

static VarStack igen_sons(Decl* d, VarStack* vs, Node* n);
static Var* igen_node(Decl* d, VarStack* vs, Node* n);

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

static Var* instr_result(Decl* d, VarStack* vs, Instr* ir) {
    if (ir) {
        int i;
        list_append(d->sons, ir);
        if (ir->dst && vs) list_append(vs->v, ir->dst);
        for (i=0; i<list_len(ir->v); ++i) {
            bassert(ir->v[i], "null variable at index %d", i);
            ++ir->v[i]->uses;
        }
        return ir->dst;
    } else return NULL;
}

static void igen_attr_chain(Decl* d, VarStack* vs, Var* v, Node* n) {
    // Make sure the original attributes come FIRST.
    if (n->kind != Nattr) {
        v->base = igen_node(d, vs, n);
        ++v->base->uses;
    } else {
        igen_attr_chain(d, vs, v, n->sons[0]);
        if (!n->attr->d) {
            // Hope it's a decl...
            igen_decl(d->m, n->attr);
            bassert(n->attr->d, "igen of unbound attribute still has no decl");
        }
        list_append(v->av, &n->attr->d->v);
    }
}

static void igen_index_chain(Decl* d, VarStack* vs, Var* v, Node* n) {
    if (n->kind != Nindex) {
        v->base = igen_node(d, vs, n);
        ++v->base->uses;
    } else {
        igen_index_chain(d, vs, v, n->sons[0]);
        list_append(v->iv, igen_node(d, vs, n->sons[1]));
        ++v->iv[list_len(v->iv)-1]->uses;
    }
}

static void igen_destr(Decl* d, Var* v) {
    Instr* ir;
    STEntry* destr;
    if (!v->type || v->type->kind != Tstruct) return;
    destr = v->type->n->magic[Mdelete];
    if (!destr) return;
    ir = new(Instr);
    ir->kind = Idel;
    list_append(ir->v, destr->overloads[0]->n->v);
    list_append(ir->v, v);
    list_append(v->destr, ir);
    instr_result(d, NULL, ir);
}

static void igen_destr_live_vars(Decl* d, VarStack* vs) {
    while (vs) {
        int i;
        for (i=0; i<list_len(vs->v); ++i) igen_destr(d, vs->v[i]);
        vs = vs->prev;
    }
}

static Var* igen_node(Decl* d, VarStack* vs, Node* n) {
    Instr* ir = new(Instr);
    Var* v;
    Type* t;
    int i, label;
    VarStack vvs;
    switch (n->kind) {
    case Nbody:
        vvs = igen_sons(d, vs, n);
        for (i=0; i<list_len(vvs.v); ++i) igen_destr(d, vvs.v[i]);
        list_free(vvs.v);
        instr_free(ir);
        ir = NULL;
        break;
    case Nreturn:
        if (n->sons) {
            ir->kind = Isr;
            list_append(ir->v, igen_node(d, vs, n->sons[0]));
            instr_result(d, vs, ir); // XXX
            ir = new(Instr);
        }
        igen_destr_live_vars(d, vs);
        ir->kind = Ijmp;
        ir->label = d->rl;
        break;
    case Nif:
        ir->kind = Icjmp;
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        ir->label = label = d->labels++;
        instr_result(d, vs, ir); // XXX
        igen_node(d, vs, n->sons[1]);

        ir = new(Instr);
        ir->kind = Ilabel;
        ir->label = label;
        break;
    case Nwhile:
        ir->kind = Ilabel;
        ir->label = label = d->labels;
        d->labels += 2;
        instr_result(d, vs, ir); // XXX

        ir = new(Instr);
        ir->kind = Icjmp;
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        ir->label = label+1;
        instr_result(d, vs, ir); // XXX

        igen_node(d, vs, n->sons[1]);

        ir = new(Instr);
        ir->kind = Ijmp;
        ir->label = label;
        instr_result(d, vs, ir); // XXX

        ir = new(Instr);
        ir->kind = Ilabel;
        ir->label = label+1;
        break;
    case Nlet:
        ir->kind = Inew;
        n->v = ir->dst = var_new(d, ir, n->type, n->s);
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Nassign:
        ir->kind = Iset;
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        list_append(ir->v, igen_node(d, vs, n->sons[1]));
        ir->flags |= PUREFLAGS(ir->v[0]) & PUREFLAGS(ir->v[1]);
        igen_destr(d, ir->v[0]);
        break;
    case Nderef:
        free(ir);
        v = var_new(d, &magic, n->type, NULL);
        v->deref = 1;
        v->base = igen_node(d, vs, n->sons[0]);
        ++v->base->uses;
        return v;
    case Naddr:
        bassert(n->sons[0]->flags & Faddr, "expected addressable node");
        ir->kind = Iaddr;
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        ir->dst = var_new(d, ir, n->type, NULL);
        break;
    case Nindex:
        free(ir);
        v = var_new(d, &magic, n->type, NULL);
        igen_index_chain(d, vs, v, n);
        return v;
    case Nnew: case Ncall:
        ir->kind = n->kind == Nnew ? Iconstr : Icall;
        v = n->sons[0]->kind == Nid && n->sons[0]->e->n->kind == Nfun &&
            (!strcmp(n->sons[0]->e->n->s->str, "[]") ||
             !strcmp(n->sons[0]->e->n->s->str, "&[]")) ?
            igen_node(d, vs, list_pop(n->sons)) : NULL;
        if (v && *n->sons[0]->e->n->s->str == '&') {
            // XXX: type lies about the result type to make type-checking work.
            t = new(Type);
            t->kind = Tptr;
            list_append(t->sons, n->type);
        } else t = n->flags & Fvoid ? NULL : n->type;
        ir->dst = var_new(d, ir, t, NULL);
        for (i=0; i<list_len(n->sons); ++i)
            list_append(ir->v, igen_node(d, vs, n->sons[i]));
        if (v) {
            ir->v[0] = var_new(d, &magic, n->sons[0]->type, NULL);
            ir->v[0]->base = v;
            igen_decl(d->m, n->sons[0]->e->n);
            bassert(n->sons[0]->e->n->d, "igen of [] has no decl");
            list_append(ir->v[0]->av, &n->sons[0]->e->n->d->v);

            if (*(*ir->v[0]->av[0])->name->str == '&') {
                // We need to add a wrapper over the result to deref it.
                v = var_new(d, &magic, n->sons[0]->type, NULL);
                v->base = instr_result(d, vs, ir);
                ++v->base->uses;
                v->deref = 1;
                return v;
            }
        }
        break;
    case Ncast:
        ir->kind = Icast;
        ir->dst = var_new(d, ir, n->type, NULL);
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        ir->flags |= PUREFLAGS(ir->v[0]);
        break;
    case Nattr:
        free(ir);
        v = var_new(d, &magic, n->type, NULL);
        igen_attr_chain(d, vs, v, n);
        return v;
    case Nop:
        ir->kind = Iop;
        ir->dst = var_new(d, ir, n->type, NULL);
        ir->op = n->op;
        list_append(ir->v, igen_node(d, vs, n->sons[0]));
        list_append(ir->v, igen_node(d, vs, n->sons[1]));
        ir->flags |= PUREFLAGS(ir->v[0]) & PUREFLAGS(ir->v[1]);
        break;
    case Ntypeof:
        bassert(n->parent->kind == Nnew, "Ntypeof outside of Nnew");
        // Fallthough.
    case Nid:
        bassert(n->e && n->e->n, "Nid has no corresponding entry");
        if (!n->e->n->v) {
            // Hope it's a decl...
            igen_decl(d->m, n->e->n);
            bassert(n->e->n->v,
                    "igen of previously unbound Nid still has no associated var");
        }
        if (n->e->n != builtins[Btrue] && n->e->n != builtins[Bfalse]) {
            free(ir);
            ++n->e->n->v->uses;
            return n->e->n->v;
        }
        // Fallthough.
    case Nint: case Nstr:
        ir->kind = n->kind == Nstr ? Istr : Iint;
        ir->flags |= Fpure;
        ir->s = n->kind == Nid
                ? string_new(n->e->n == builtins[Btrue] ? "1/*true*/"
                                                        : "0/*false*/")
                : string_clone(n->s);
        ir->dst = var_new(d, ir, n->type, NULL);
        break;
    case Nmodule: case Nstruct: case Nfun: case Narglist: case Ndecl: case Nsons:
    case Nptr:
        fatal("unexpected node kind %d", n->kind);
    }

    return instr_result(d, vs, ir);
}

static VarStack igen_sons(Decl* d, VarStack* vs, Node* n) {
    int i;
    bassert(n && n->kind > Nsons, "unexpected son-less node kind %d",
            n?n->kind:-1);
    VarStack vvs;
    vvs.prev = vs;
    vvs.v = NULL;
    for (i=0; i<list_len(n->sons); ++i) igen_node(d, &vvs, n->sons[i]);

    return vvs;
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

    if (n->bind) d->v->flags |= Fstc;

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
        if (n->sons[0]->type->kind == Tbuiltin ||
            n->sons[0]->type->kind == Tptr) {
            d->ret = n->sons[0]->type;
            d->ra = 0;
        } else {
            Type* t = new(Type);
            t->kind = Tptr;
            list_append(t->sons, n->sons[0]->type);
            list_append(d->m->types, t);
            d->ret = t;
            d->ra = 1;
        }
        d->rv = var_new(d, NULL, d->ret, NULL);
    }

    if (!n->import) {
        Instr* ir = new(Instr);
        d->rl = d->labels++;
        igen_node(d, NULL, n->sons[2]);
        ir->kind = Ilabel;
        ir->label = d->rl;
        instr_result(d, NULL, ir);
    } else d->import = n->import;

    d->exportc = n->exportc;
}

static void igen_global(Module* m, Decl* d, Node* n) {
    bassert(n->kind == Ndecl, "unexpected node kind %d", n->kind);
    d->kind = Dglobal;
    d->v = n->v = var_new(d, NULL, n->type, n->s);
    ++d->v->uses;

    if (n->sons[1]) {
        Instr* set = new(Instr);

        set->kind = Iset;
        list_append(set->v, d->v);
        list_append(set->v, igen_node(m->init, NULL, n->sons[1]));
        ++set->v[1]->uses;
        list_append(m->init->sons, set);
    } else if (n->import) d->import = n->import;
}

static Decl* igen_decl(Module* m, Node* n) {
    if (n->d) return n->d;
    n->d = new(Decl);
    if (n->s) n->d->name = string_clone(n->s);
    if (n->export) n->d->export = 1;
    n->d->m = m;

    switch (n->kind) {
    case Nfun:
        igen_func(m, n->d, n);
        break;
    case Ndecl:
        igen_global(m, n->d, n);
        break;
    case Nstruct: default:
        if (n->d->name) string_free(n->d->name);
        free(n->d);
        n->d = NULL;
        return NULL;
    }

    return n->d;
}

Module* igen(Node* n) {
    Module* res;
    int i;
    bassert(n && n->kind == Nmodule, "unexpected node kind %d", n?n->kind:-1);

    memset(&magic, 0, sizeof(magic));

    res = n->m = new(Module);
    #ifndef NO_BUILTINS
    if (n != builtins_module) list_append(res->imports, builtins_module->m);
    #endif

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
