#include "blaze.h"

static void force_type_context(Node* n) {
    if (!(n->flags & Ftype)) {
        error(n->loc, "expression is not a type");
        if (n->e && n->e->n) declared_here(n->e->n);
        n->e = anytype;
        n->type = anytype->override;
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
    case Tany: assert(0);
    }
}

static int types_are_compat(Type* a, Type* b) {
    int i;
    assert(a && b);
    if (a->kind == Tany || b->kind == Tany) return 1;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case Tbuiltin: return a->bkind == b->bkind;
    case Tptr: return types_are_compat(a->sons[0], b->sons[0]);
    case Tfun:
        if (list_len(a->sons) != list_len(b->sons)) return 0;
        for (i=0; i<list_len(a->sons); ++i)
            if (!types_are_compat(a->sons[i], b->sons[i])) return 0;
        return 1;
    case Tany: assert(0);
    }
}

static Node* get_function(Node* n) {
    while (n->kind != Nfun) n = n->parent;
    return n;
}

void type_free(Type* t, Node* user) {
    int i, cleared=0, active=0;
    if (!t || ((t->kind == Tany || t->kind == Tbuiltin) && user)) return;
    for (i=0; i<list_len(t->users); ++i)
        if (t->users[i] == user && !cleared) {
            t->users[i] = NULL;
            cleared = 1;
        } else if (t->users[i]) ++active;
    if (active && t->kind != Tany && t->kind != Tbuiltin) return;
    switch (t->kind) {
    case Tany: case Tbuiltin: string_free(t->name); break;
    default: break;
    }
    list_free(t->sons);
    list_free(t->users);
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
    case Nfun:
        if (n->sons[0]) {
            type(n->sons[0]);
            force_type_context(n->sons[0]);
        }
        type(n->sons[1]);
        n->type = new(Type);
        n->type->kind = Tfun;
        list_append(n->type->users, n);
        if (n->sons[0]) {
            list_append(n->type->sons, n->sons[0]->type);
            list_append(n->sons[0]->type->users, n);
        }
        else list_append(n->type->sons, NULL);
        for (i=0; i<list_len(n->sons[1]->sons); ++i) {
            list_append(n->type->sons, n->sons[1]->sons[i]->type);
            list_append(n->sons[1]->sons[i]->type->users, n);
        }
        type(n->sons[2]);
        break;
    case Nlet:
        type(n->sons[0]);
        force_typed_expr_context(n->sons[0]);
        n->type = n->sons[0]->type;
        list_append(n->type->users, n);
        // XXX: This should NOT be here! It should be in a resolve-related pass.
        if (!(n->flags & Fused))
            warning(n->loc, "unused variable '%s'", n->s->str);
        break;
    case Nassign:
        type(n->sons[0]);
        type(n->sons[1]);
        if (!types_are_compat(n->sons[0]->type, n->sons[1]->type)) {
            String* ls=typestring(n->sons[0]->type),
                  * rs=typestring(n->sons[1]->type);
            error(n->loc, "types '%s' and '%s' in assignment are not compatible",
                  ls->str, rs->str);
            declared_here(n->sons[0]);
            declared_here(n->sons[1]);
            string_free(ls);
            string_free(rs);
        }
        break;
    case Nreturn:
        f = get_function(n);
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
            if (!types_are_compat(ret, given)) {
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
        } else {
            n->type = n->sons[0]->type;
            list_append(n->type->users, n);
        }
        n->flags |= Ftype;
        break;
    case Narg:
        type(n->sons[0]);
        force_type_context(n->sons[0]);
        n->type = n->sons[0]->type;
        list_append(n->type->users, n);
        break;
    case Nptr:
        type(n->sons[0]);
        force_type_context(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else {
            n->type = new(Type);
            n->type->kind = Tptr;
            list_append(n->type->sons, n->sons[0]->type);
            list_append(n->type->users, n);
        }
        n->flags |= Ftype;
        break;
    case Nderef:
        type(n->sons[0]);
        force_typed_expr_context(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->sons[0]->type->kind == Tptr) {
            n->type = n->sons[0]->type->sons[0];
            list_append(n->type->users, n);
        }
        else {
            String* ts = typestring(n->sons[0]->type);
            error(n->sons[0]->loc, "expected pointer type, got '%s'", ts->str);
            declared_here(n->sons[0]);
            string_free(ts);
            n->type = anytype->override;
        }
        break;
    case Naddr:
        type(n->sons[0]);
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else {
            n->type = new(Type);
            n->type->kind = Tptr;
            list_append(n->type->sons, n->sons[0]->type);
            list_append(n->type->users, n);
        }
        break;
    case Ncall:
        for (i=0; i<list_len(n->sons); ++i) {
            type(n->sons[i]);
            force_typed_expr_context(n->sons[i]);
        }
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->sons[0]->type->kind != Tfun) {
            error(n->loc, "cannot call non-function");
            declared_here(n->sons[0]);
        } else {
            Type* ft = n->sons[0]->type;
            int ngiven = list_len(n->sons)-1, nexpect = list_len(ft->sons)-1;
            if (ngiven != nexpect) {
                error(n->loc, "function expected %d argument(s), not %d",
                      nexpect, ngiven);
                declared_here(n->sons[0]);
            }
            for (i=1; i<min(ngiven+1, nexpect+1); ++i)
                if (!types_are_compat(n->sons[i]->type, ft->sons[i])) {
                    String* expects, *givens;
                    expects = typestring(ft->sons[i]);
                    givens = typestring(n->sons[i]->type);
                    error(n->sons[i]->loc, "function expected argument of type "
                                           "'%s', not '%s'", expects->str,
                                           givens->str);
                    declared_here(n->sons[0]);
                    declared_here(n->sons[i]);
                }
            if (ft->sons[0]){
                n->type = ft->sons[0];
                list_append(n->type->users, n);
            }
            else {
                n->type = anytype->override;
                n->flags |= Fvoid;
            }
        }
        break;
    case Nid:
        if (n->e) {
            if (n->e->n) {
                n->type = n->e->n->type;
                if (n->type) list_append(n->type->users, n);
                n->flags |= n->e->n->flags & Ftype;
            } else {
                n->type = n->e->override;
                // No need to worry about users here, since this is a builtin.
                n->flags |= Ftype;
            }
        } else n->type = anytype->override;
        break;
    case Nint:
        n->type = builtin_types[Tint]->override;
        break;
    case Nsons: assert(0);
    }
}
