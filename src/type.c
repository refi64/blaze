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

static int typematch(Type* a, Type* b) {
    int i;
    assert(a && b);
    if (a->kind == Tany || b->kind == Tany) return 1;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case Tbuiltin: return a->bkind == b->bkind;
    case Tptr: return typematch(a->sons[0], b->sons[0]);
    case Tfun:
        if (list_len(a->sons) != list_len(b->sons)) return 0;
        for (i=0; i<list_len(a->sons); ++i)
            if (!typematch(a->sons[i], b->sons[i])) return 0;
        return 1;
    case Tstruct: return a == b;
    case Tany: assert(0);
    }
}

static int is_callable(Node* n) {
    return n->type && (n->type->kind == Tfun || n->flags & Ftype);
}

static List(Type*) arguments_of(Type* t) {
    switch (t->kind) {
    case Tstruct: return t->constr->sons;
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
    switch (n->kind) {
    case Nmodule: case Narglist: case Nbody:
        for (i=0; i<list_len(n->sons); ++i) type(n->sons[i]);
        break;
    case Nstruct:
        n->type = new(Type);
        n->type->kind = Tstruct;
        n->type->name = string_clone(n->s);
        type_incref(n->type);
        n->flags |= Ftype;
        for (i=0; i<list_len(n->sons); ++i) {
            type(n->sons[i]);
            if (n->sons[i]->kind == Nconstr) {
                if (!n->constr) {
                    n->constr = n->sons[i];
                    n->type->constr = n->sons[i]->type;
                }
                else {
                    error(n->sons[i]->loc, "duplicate constructor");
                    note(n->constr->loc, "previous constructor here");
                }
            }
        }
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
        } else if (n->kind == Nconstr) {
            assert(n->parent->flags & Ftype);
            list_append(n->type->sons, n->parent->type);
            type_incref(n->type->sons[0]);
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
        if (!typematch(n->sons[0]->type, n->sons[1]->type)) {
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
                return;
            }
            given = n->sons[0]->type;
            if (!typematch(ret, given)) {
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
            error(n->sons[0]->loc, "function should not return a value");
            note(f->loc, "function declared here");
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
        }
        type_incref(n->type);
        n->flags |= Ftype;
        break;
    case Nderef:
        type(n->sons[0]);
        force_typed_expr_context(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->sons[0]->type->kind == Tptr)
            n->type = n->sons[0]->type->sons[0];
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
            type_incref(n->sons[0]->type);
        }
        type_incref(n->type);
        break;
    case Ncall:
        for (i=0; i<list_len(n->sons); ++i) {
            type(n->sons[i]);
            if (!(n->sons[i]->flags & Ftype))
                force_typed_expr_context(n->sons[i]);
        }
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (!is_callable(n->sons[0])) {
            String* ts = typestring(n->sons[0]->type);
            error(n->loc, "cannot call non-callable type '%s'", ts->str);
            string_free(ts);
            declared_here(n->sons[0]);
            n->type = anytype->override;
        } else {
            Type* ft = n->sons[0]->type;
            List(Type*) expected = arguments_of(ft);
            int ngiven = list_len(n->sons)-1, nexpect = list_len(expected)-1;
            if (ngiven != nexpect) {
                error(n->loc, "function expected %d argument(s), not %d",
                      nexpect, ngiven);
                declared_here(n->sons[0]);
            }
            for (i=1; i<min(ngiven+1, nexpect+1); ++i)
                if (!typematch(n->sons[i]->type, expected[i])) {
                    String* expects, *givens;
                    expects = typestring(expected[i]);
                    givens = typestring(n->sons[i]->type);
                    error(n->sons[i]->loc, "function expected argument of type "
                                           "'%s', not '%s'", expects->str,
                                           givens->str);
                    declared_here(n->sons[0]);
                    declared_here(n->sons[i]);
                    string_free(expects);
                    string_free(givens);
                }
            if (expected[0]) n->type = expected[0];
            else {
                n->type = anytype->override;
                n->flags |= Fvoid;
            }
        }
        type_incref(n->type);
        break;
    case Nattr: break; // XXX
    case Nid:
        if (n->e) {
            if (n->e->n) {
                n->type = n->e->n->type;
                n->flags |= n->e->n->flags & Ftype;
            } else {
                n->type = n->e->override;
                n->flags |= Ftype;
            }
        } else n->type = anytype->override;
        if (n->type) type_incref(n->type);
        break;
    case Nint:
        n->type = builtin_types[Tint]->override;
        type_incref(n->type);
        break;
    case Nsons: assert(0);
    }
}
