#include "blaze.h"

static void force_type_context(Node* n) {
    if (!(n->flags & Ftype) && n->type != anytype->override) {
        error(n->loc, "expression is not a type");
        if (n->e && n->e->n) declared_here(n->e->n);
        n->e = anytype;
        type_decref(n->type);
        n->type = anytype->override;
        type_incref(n->type);
    }
}

static void force_typed_expr_context(Node* n) {
    if (n->flags & Ftype) {
        error(n->loc, "expected expression, not type");
        if (n->e && n->e->n) declared_here(n->e->n);
        type_decref(n->type);
        n->type = anytype->override;
        type_incref(n->type);
    } else if (n->flags & Fvoid) {
        error(n->loc, "cannot use void value in expression");
        if (n->kind == Ncall) declared_here(n->sons[0]);
    }
}

static String* typestring(Type* t) {
    String* res, *s;
    int i;
    char* p;
    switch (t->kind) {
    case Tbuiltin:
        switch (t->bkind) {
        case Tint: return string_new("int");
        case Tchar: return string_new("char");
        case Tbyte: return string_new("byte");
        case Tsize: return string_new("size");
        case Tbool: return string_new("bool");
        case Tbend: assert(0);
        }
    case Tptr:
        res = string_newz("*", 1);
        s = typestring(t->sons[0]);
        if ((p = strchr(s->str, ' '))) string_mergec(res, '(');
        string_merge(res, s);
        if (p) string_mergec(res, ')');
        string_free(s);
        return res;
    case Tfun:
        res = string_newz("(", 1);
        for (i=1; i<list_len(t->sons); ++i) {
            if (i != 1) string_merges(res, ", ");
            s = typestring(t->sons[i]);
            string_merge(res, s);
            string_free(s);
        }
        string_merges(res, ") -> ");
        if (t->sons[0]) {
            s = typestring(t->sons[0]);
            string_merge(res, s);
            string_free(s);
        }
        return res;
    case Tstruct: return string_clone(t->name);
    case Tany: assert(0);
    }
}

static int typematch(Type* a, Type* b, Node* ctx) {
    int i;
    assert(a && b);
    if (a->kind == Tany || b->kind == Tany) return 1;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case Tbuiltin:
        if (ctx && ctx->kind == Nint && a->bkind != Tbool && b->bkind != Tbool)
            return 1;
        else return a->bkind == b->bkind;
    case Tptr: return typematch(a->sons[0], b->sons[0], NULL);
    case Tfun:
        if (list_len(a->sons) != list_len(b->sons)) return 0;
        for (i=0; i<list_len(a->sons); ++i)
            if (!typematch(a->sons[i], b->sons[i], NULL)) return 0;
        return 1;
    case Tstruct: return a == b;
    case Tany: assert(0);
    }
}

static int is_callable(Node* n) {
    return n->type && n->type->kind == Tfun;
}

static List(Type*) arguments_of(Type* t) {
    switch (t->kind) {
    case Tstruct: return (*t->constr)->sons;
    case Tfun: return t->sons;
    default: assert(0);
    }
}

void type_incref(Type* t) {
    assert(t);
    ++t->rc;
}

void type_decref(Type* t) {
    int i;
    assert(t && t->rc);
    if (--t->rc) return;
    switch (t->kind) {
    case Tany: case Tbuiltin: case Tstruct: string_free(t->name); break;
    default: break;
    }
    for (i=0; i<list_len(t->sons); ++i) if (t->sons[i]) type_decref(t->sons[i]);
    list_free(t->sons);
    free(t);
}

void type(Node* n) {
    Node* f;
    int i;

    assert(n);
    if (n->type) return;
    else if (n->typing) {
        error(n->loc, "type is recursive");
        n->type = anytype->override;
        type_incref(n->type);
        n->typing = 0;
        return;
    } else n->typing = 1;

    switch (n->kind) {
    case Nmodule: case Narglist: case Nbody:
        for (i=0; i<list_len(n->sons); ++i) type(n->sons[i]);
        break;
    case Nstruct:
        n->type = new(Type);
        n->type->kind = Tstruct;
        n->type->name = string_clone(n->s);
        n->type->n = n;
        type_incref(n->type);
        n->this->type = n->type;
        type_incref(n->type);

        n->flags |= Ftype;
        n->typing = 0;
        for (i=0; i<list_len(n->sons); ++i) {
            if (n->sons[i]->kind == Nconstr) {
                if (!n->constr) {
                    n->constr = n->sons[i];
                    n->type->constr = &n->constr->type;
                }
                else {
                    error(n->sons[i]->loc, "duplicate constructor");
                    note(n->constr->loc, "previous constructor here");
                }
            }

            if (n->sons[i]->this) {
                n->sons[i]->this->type = n->type;
                type_incref(n->type);
            }

            type(n->sons[i]);
        }

        if (!n->constr)
            error(n->loc, "struct must have a constructor");
        break;
    case Nconstr: case Nfun:
        if (n->sons[0]) {
            type(n->sons[0]);
            force_type_context(n->sons[0]);
        }
        type(n->sons[1]);
        n->type = new(Type);
        n->type->kind = Tfun;
        type_incref(n->type);
        if (n->sons[0]) {
            list_append(n->type->sons, n->sons[0]->type);
            type_incref(n->sons[0]->type);
        } else list_append(n->type->sons, NULL);
        for (i=0; i<list_len(n->sons[1]->sons); ++i) {
            list_append(n->type->sons, n->sons[1]->sons[i]->type);
            type_incref(n->sons[1]->sons[i]->type);
        }
        if (!n->import) type(n->sons[2]);
        break;
    case Nlet:
        type(n->sons[0]);
        force_typed_expr_context(n->sons[0]);
        n->type = n->sons[0]->type;
        type_incref(n->type);
        // XXX: This should NOT be here! It should be in a resolve-related pass.
        if (!(n->flags & Fused))
            warning(n->loc, "unused variable '%s'", n->s->str);
        break;
    case Nassign:
        type(n->sons[0]);
        type(n->sons[1]);
        if (n->sons[0]->flags & Fcst) {
            error(n->sons[0]->loc, "left-hand side of assignment cannot be "
                                   "constant");
            declared_here(n->sons[0]);
        } else if (!(n->sons[0]->flags & Faddr)) {
            error(n->sons[0]->loc, "left-hand side of assignment must be "
                                   "addressable");
            declared_here(n->sons[0]);
        } else if (!(n->sons[0]->flags & Fvar)) {
            Node* d;
            error(n->sons[0]->loc, "left-hand side of assignment must be "
                                   "variable");
            d = declared_here(n->sons[0]);
            if (n->sons[0] != d) make_mutvar(d, Fvar, n->sons[0]->flags);
        }
        if (!typematch(n->sons[0]->type, n->sons[1]->type, n->sons[1])) {
            String* ls=typestring(n->sons[0]->type),
                  * rs=typestring(n->sons[1]->type);
            error(n->loc, "types '%s' and '%s' in assignment are not compatible",
                  ls->str, rs->str);
            declared_here(n->sons[0]);
            declared_here(n->sons[1]);
            string_free(ls);
            string_free(rs);
        }
        n->type = n->sons[0]->type;
        type_incref(n->type);
        break;
    case Nreturn:
        f = n->func;
        assert(f);
        if (n->sons) {
            type(n->sons[0]);
            force_typed_expr_context(n->sons[0]);
        }
        if (f->sons[0]) {
            Type* ret = f->type->sons[0], *given;
            if (!n->sons) {
                error(n->loc, "function is supposed to return a value");
                note(f->sons[0]->loc, "function return type declared here");
                break;
            }
            given = n->sons[0]->type;
            if (!typematch(ret, given, n->sons[0])) {
                String* rets, *givens;
                rets = typestring(ret);
                givens = typestring(given);
                error(n->sons[0]->loc, "function was declared to return type '%s'"
                                       ", not '%s'", rets->str, givens->str);
                note(f->sons[0]->loc, "function return type declared here");
                declared_here(n->sons[0]);
                string_free(rets);
                string_free(givens);
            }
        } else if (n->sons) {
            const char* k = f->kind == Nconstr ? "constructor" : "function";
            error(n->sons[0]->loc, "%s should not return a value", k);
            note(f->loc, "%s declared here", k);
        }
        break;
    case Ntypeof:
        type(n->sons[0]);
        if (!n->sons[0]->type) {
            error(n->sons[0]->loc, "type of expression is recursive");
            declared_here(n->sons[0]);
            n->type = anytype->override;
        } else n->type = n->sons[0]->type;
        n->flags |= Ftype;
        type_incref(n->type);
        break;
    case Ndecl:
        type(n->sons[0]);
        force_type_context(n->sons[0]);
        n->type = n->sons[0]->type;
        type_incref(n->type);

        if (n->sons[1]) {
            type(n->sons[1]);
            force_typed_expr_context(n->sons[1]);
            if (!typematch(n->type, n->sons[1]->type, n->sons[1])) {
                String* expects, *givens;
                expects = typestring(n->type);
                givens = typestring(n->sons[1]->type);
                error(n->sons[1]->loc, "types '%s' and '%s' in assignment are not"
                                       " compatible", expects->str, givens->str);
                declared_here(n->sons[1]);
            }
        }
        break;
    case Nptr:
        type(n->sons[0]);
        force_type_context(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else {
            n->type = new(Type);
            n->type->kind = Tptr;
            list_append(n->type->sons, n->sons[0]->type);
            type_incref(n->sons[0]->type);
            if (n->flags & Fmut) n->type->mut = 1;
        }
        type_incref(n->type);
        n->flags |= Ftype;
        break;
    case Nderef:
        type(n->sons[0]);
        force_typed_expr_context(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->sons[0]->type->kind == Tptr) {
            n->type = n->sons[0]->type->sons[0];
            if (n->sons[0]->type->mut) n->flags |= Fmv;
        }
        else {
            String* ts = typestring(n->sons[0]->type);
            error(n->sons[0]->loc, "expected pointer type, got '%s'", ts->str);
            declared_here(n->sons[0]);
            string_free(ts);
            n->type = anytype->override;
        }
        type_incref(n->type);
        break;
    case Naddr:
        type(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else {
            n->type = new(Type);
            n->type->kind = Tptr;
            list_append(n->type->sons, n->sons[0]->type);
            if (n->sons[0]->flags & Fvar) n->type->mut = 1;
            type_incref(n->sons[0]->type);
        }
        type_incref(n->type);
        break;
    case Nindex:
        type(n->sons[0]);
        type(n->sons[1]);
        if (n->sons[0]->type->kind != Tptr) {
            String* ts = typestring(n->sons[0]->type);
            error(n->loc, "only pointers can be indexed, not '%s'", ts->str);
            string_free(ts);
            n->type = anytype->override;
        } else if (!typematch(builtin_types[Tsize]->override, n->sons[1]->type,
                              n->sons[1])) {
            String* ts = typestring(n->sons[1]->type);
            error(n->loc, "only integral types can be indices, not '%s'",
                  ts->str);
            string_free(ts);
            n->type = anytype->override;
        } else n->type = n->sons[0]->type->sons[0];
        type_incref(n->type);
        if (n->sons[0]->type->mut)
            n->flags |= Fmv;
        break;
    case Nnew: case Ncall:
        for (i=0; i<list_len(n->sons); ++i) {
            type(n->sons[i]);
            if (n->kind == Nnew && i == 0) force_type_context(n->sons[i]);
            else force_typed_expr_context(n->sons[i]);
        }
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->kind == Ncall && !is_callable(n->sons[0])) {
            String* ts = typestring(n->sons[0]->type);
            error(n->loc, "cannot call non-callable type '%s'", ts->str);
            string_free(ts);
            declared_here(n->sons[0]);
            n->type = anytype->override;
        } else {
            const char* msgs[] = {"function", "constructor"};
            Type* ft = n->sons[0]->type;
            List(Type*) expected = NULL;
            int ngiven = list_len(n->sons)-1, nexpect;
            if (ft->kind == Tstruct && !ft->constr) {
                expected = NULL;
                nexpect = 0;
            } else {
                expected = arguments_of(ft);
                nexpect = list_len(expected)-1;
            }
            if (ngiven != nexpect) {
                error(n->loc, "%s expected %d argument(s), not %d",
                      msgs[n->kind == Nconstr], nexpect, ngiven);
                declared_here(n->sons[0]);
            }
            for (i=1; i<min(ngiven+1, nexpect+1); ++i)
                if (!typematch(expected[i], n->sons[i]->type, n->sons[i])) {
                    String* expects, *givens;
                    expects = typestring(expected[i]);
                    givens = typestring(n->sons[i]->type);
                    error(n->sons[i]->loc, "%s expected argument of type '%s', "
                                           "not '%s'", msgs[n->kind == Nconstr],
                                           expects->str, givens->str);
                    declared_here(n->sons[0]);
                    declared_here(n->sons[i]);
                    string_free(expects);
                    string_free(givens);
                }
            if (expected && expected[0]) n->type = expected[0];
            else if (n->kind == Nnew) {
                assert(n->sons[0]->flags & Ftype);
                n->type = n->sons[0]->type;
            } else {
                n->type = anytype->override;
                n->flags |= Fvoid;
            }
        }
        type_incref(n->type);
        break;
    case Ncast:
        type(n->sons[0]);
        type(n->sons[1]);
        force_typed_expr_context(n->sons[0]);
        force_type_context(n->sons[1]);
        if (n->sons[1]->type == anytype->override) n->type = anytype->override;
        else n->type = n->sons[1]->type;
        type_incref(n->type);
        break;
    case Nattr:
        type(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->sons[0]->type->kind != Tstruct) {
            String* ts = typestring(n->sons[0]->type);
            error(n->sons[0]->loc, "'%s' is not a struct", ts->str);
            declared_here(n->sons[0]);
            n->type = anytype->override;
        } else {
            STEntry* e = symtab_findl(n->sons[0]->type->n->tab, n->s);
            if (!e) {
                error(n->loc, "undefined attribute '%s'", n->s->str);
                declared_here(n->sons[0]);
                n->type = anytype->override;
            } else {
                int flags = 0;

                assert(e->n);
                n->type = e->n->type;
                n->attr = e->n;
                if (n->sons[0]->flags & Fmv) flags |= Fvar;
                flags &= e->n->flags & Fmv;
                n->flags |= flags;
            }
        }
        type_incref(n->type);
        break;
    case Nop:
        type(n->sons[0]);
        type(n->sons[1]);
        if (n->op > Orelop) {
            if (!typematch(n->sons[0]->type, n->sons[1]->type, n->sons[1]) &&
                !typematch(n->sons[1]->type, n->sons[0]->type, n->sons[0])) {
                String* tl, *tr;
                tl = typestring(n->sons[0]->type);
                tr = typestring(n->sons[1]->type);
                error(n->loc, "invalid types '%s' and '%s' in comparison "
                              "expression", tl->str, tr->str);
                string_free(tl);
                string_free(tr);
            }

            n->type = builtin_types[Tbool]->override;
        }
        else {
            if (n->sons[0]->type->kind != Tbuiltin ||
                n->sons[1]->type->kind != Tbuiltin)
                n->type = NULL;
            else if (typematch(n->sons[0]->type, n->sons[1]->type, n->sons[1]))
                n->type = n->sons[0]->type;
            else if (typematch(n->sons[1]->type, n->sons[0]->type, n->sons[0]))
                n->type = n->sons[1]->type;

            if (!n->type) {
                String* tl, *tr;
                tl = typestring(n->sons[0]->type);
                tr = typestring(n->sons[1]->type);
                error(n->loc, "invalid types '%s' and '%s' in binary expression",
                      tl->str, tr->str);
                string_free(tl);
                string_free(tr);
                n->type = anytype->override;
            }
        }
        type_incref(n->type);
        break;
    case Nid:
        if (n->e) {
            if (n->e->n) {
                type(n->e->n);
                n->type = n->e->n->type;
                n->flags |= n->e->n->flags & Ftype;
            } else {
                n->type = n->e->override;
                n->flags |= Ftype;
            }
        } else n->type = anytype->override;
        type_incref(n->type);
        break;
    case Nint:
        n->type = builtin_types[Tint]->override;
        type_incref(n->type);
        break;
    case Nsons: assert(0);
    }

    n->typing = 0;
}
