#include "blaze.h"

static void force_type_context(Node* n) {
    switch (n->kind) {
    case Nmodule: case Nfun: case Nbody: case Narglist: case Narg: case Nreturn:
    case Nlet: case Nassign: case Nint: case Nsons: assert(0);
    case Ntypeof:
        // XXX: This is a hack!
        if (!n->type) {
            error(n->sons[0]->loc, "type of expression is recursive");
            declared_here(n->sons[0]);
            n->type = anytype->override;
        }
        break;
    case Nptr:
        if (n->sons[0]->type == anytype->override) {
            type_free(n->type, n);
            n->type = anytype->override;
        }
        break;
    case Nid:
        if (!n->e) n->e = anytype;
        else if (n->e->n && !(n->e->n->flags & Ftype)) {
            error(n->loc, "%s is not a type", n->s->str);
            declared_here(n->e->n);
            n->e = anytype;
        }
        n->type = n->e->n ? n->e->n->type : n->e->override;
        break;
    }
    n->flags |= Ftype;
}

static void force_typed_expr_context(Node* n) {
    assert(n->type);

    switch (n->kind) {
    case Nmodule: case Nfun: case Nbody: case Narglist: case Narg: case Nreturn:
    case Nlet: case Nassign: case Ntypeof: case Nsons: assert(0);
    case Nptr:
        force_typed_expr_context(n->sons[0]);
        break;
    case Nid:
        if (n->e && (!n->e->n || n->e->n->flags & Ftype)) {
            error(n->loc, "expected expression, not type");
            if (n->e->n) declared_here(n->e->n);
        }
    case Nint: break;
    }
}

static String* typestring(Type* t) {
    String* res, *s;
    int i;
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
        string_merge(res, s);
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

void type_free(Type* t, Node* owner) {
    if (!t || t->owner != owner) return;
    switch (t->kind) {
    case Tany: case Tbuiltin: string_free(t->name); break;
    default: break;
    }
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
    case Nfun:
        if (n->sons[0]) {
            type(n->sons[0]);
            force_type_context(n->sons[0]);
        }
        type(n->sons[1]);
        n->type = new(Type);
        n->type->kind = Tfun;
        n->type->owner = n;
        if (n->sons[0]) list_append(n->type->sons, n->sons[0]->type);
        else list_append(n->type->sons, NULL);
        for (i=0; i<list_len(n->sons[1]->sons); ++i)
            list_append(n->type->sons, n->sons[1]->sons[i]->type);
        type(n->sons[2]);
        break;
    case Nlet:
        type(n->sons[0]);
        force_typed_expr_context(n->sons[0]);
        n->type = n->sons[0]->type;
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
        n->type = n->sons[0]->type;
        break;
    case Narg:
        force_type_context(n->sons[0]);
        n->type = n->sons[0]->type;
        break;
    case Nptr:
        type(n->sons[0]);
        n->type = new(Type);
        n->type->kind = Tptr;
        n->type->owner = n;
        force_type_context(n->sons[0]);
        list_append(n->type->sons, n->sons[0]->type);
        break;
    case Nid:
        n->type = n->e && n->e->n ? n->e->n->type : anytype->override;
        break;
    case Nint:
        n->type = builtin_types[Tint]->override;
        break;
    case Nsons: assert(0);
    }
}
