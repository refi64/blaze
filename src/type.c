/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

static void force_type_context(Node* n) {
    if (!(n->flags & Ftype)) {
        if (n->type != anytype->override) {
            error(n->loc, "expression is not a type");
            if (n->e && n->e->n) declared_here(n->e->n);
        }
    } else if (n->e && n->e->n && n->e->n->tv && n->parent->kind != Ninst) {
        error(n->loc, "expected non-generic type");
        declared_here(n);
    } else return;


    n->e = anytype;
    type_decref(n->type);
    n->type = anytype->override;
    type_incref(n->type);
    n->flags |= Ftype;
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
        type_decref(n->type);
        n->type = anytype->override;
        type_incref(n->type);
    }
}

static String* typestring(Type* t) {
    String* res, *s;
    int i;
    char* p;
    bassert(t, "expected non-null type");
    switch (t->kind) {
    case Tbuiltin:
        switch (t->bkind) {
        case Tint: return string_new("int");
        case Tchar: return string_new("char");
        case Tbyte: return string_new("byte");
        case Tsize: return string_new("size");
        case Tbool: return string_new("bool");
        case Tbend: fatal("unexpected builtin type kind Tbend");
        }
    case Tptr:
        res = string_newz("*", 1);
        if (t->mut) string_merges(res, "mut ");
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
    case Tstruct: case Tvar: return string_clone(t->name);
    case Tinst:
        res = typestring(t->base);
        string_mergec(res, '[');
        for (i=0; i<list_len(t->sons); ++i) {
            if (i) string_merges(res, ", ");
            s = typestring(t->sons[i]);
            string_merge(res, s);
            string_free(s);
        }
        string_mergec(res, ']');
        return res;
    case Tany: fatal("unexpected type kind %d", t->kind);
    }
}

static int typematch(Type* a, Type* b, Node* ctx) {
    int i;
    bassert(a && b, "expected non-null types");
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
    case Tinst:
        if (typematch(a->base, b->base, NULL)) return 1;
        else if (list_len(a->sons) != list_len(b->sons)) return 0;
        else {
            for (i=0; i<list_len(a->sons); ++i)
                if (!typematch(a->sons[i], b->sons[i], NULL)) return 0;
            return 1;
        }
    case Tany: case Tvar: fatal("unexpected type kind %d", a->kind);
    }
}

static int is_callable(Node* n) {
    return n->type && n->type->kind == Tfun;
}

void type_incref(Type* t) {
    bassert(t, "expected non-null type");
    ++t->rc;
}

void type_decref(Type* t) {
    int i;
    bassert(t && t->rc, "unbalanced reference count");
    if (--t->rc) return;
    switch (t->kind) {
    case Tany: case Tbuiltin: case Tstruct: string_free(t->name); break;
    default: break;
    }
    for (i=0; i<list_len(t->sons); ++i) if (t->sons[i]) type_decref(t->sons[i]);
    list_free(t->sons);
    free(t);
}

Type* skip(Type* t) {
    while (t->kind == Tinst) t = t->base;
    return t;
}

typedef enum Match {
    Mnothing,
    Merror,
    Mnote,
} Match;

static int funmatch(Match kind, Node* func, Node* n, List(Type*)* expected,
                    int strict) {
    Type* ft = skip(func->type);
    *expected = NULL;
    int ngiven = list_len(n->sons)-1, nexpect, i, res = 1;
    if (ft->kind == Tstruct && !ft->n->magic[Mnew]) {
        *expected = NULL;
        nexpect = 0;
    } else if (ft->kind == Tany) {
        switch (kind) {
        case Mnote:
            note(func->loc, "cannot determine if function matches because its "
                            "definition is erroneous");
            // Fallthrough.
        case Mnothing:
        case Merror:
            return 0;
        }
    } else {
        bassert(ft->kind == Tfun, "unexpected type kind %d", ft->kind);
        *expected = ft->sons;
        nexpect = list_len(*expected)-1;
    }

    if (ngiven != nexpect) {
        switch (kind) {
        case Merror:
            error(n->loc, "function expected %d argument(s), not %d", nexpect,
                  ngiven);
            declared_here(func);
            break;
        case Mnote:
            note(func->loc, "function expected %d argument(s), not %d", nexpect,
                 ngiven);
            break;
        case Mnothing: break;
        }
        return 0;
    }

    for (i=1; i<min(ngiven+1, nexpect+1); ++i)
        if (!typematch((*expected)[i], n->sons[i]->type,
                                       strict ? NULL : n->sons[i])) {
            String* expects, *givens;
            expects = typestring((*expected)[i]);
            givens = typestring(n->sons[i]->type);
            switch (kind) {
            case Mnothing: break;
            case Mnote:
                note(func->loc, "function expected argument of type '%s', "
                                "not '%s'", expects->str, givens->str);
                break;
            case Merror:
                error(n->sons[i]->loc, "function expected argument of type '%s', "
                                       "not '%s'", expects->str, givens->str);
                declared_here(func);
                break;
            }
            if (kind != Mnothing) declared_here(n->sons[i]);
            res = 0;
            string_free(expects);
            string_free(givens);
        }


    if (n->sons[0]->kind == Nattr && func->flags & Fmvm &&
        !(n->sons[0]->sons[0]->flags & Fmut) && res) {
        switch (kind) {
        case Merror:
            error(n->loc, "function requires a mutable this");
            declared_here(func);
            make_mutvar(declared_here(n->sons[0]->sons[0]), Fmut,
                        n->sons[0]->sons[0]->flags);
            break;
        case Mnote:
            note(func->loc, "function requires a mutable this");
            make_mutvar(declared_here(n->sons[0]->sons[0]), Fmut,
                        n->sons[0]->sons[0]->flags);
            break;
        case Mnothing: break;
        }
        res = 0;
    }

    return res;
}

static void resolve_overload(Node* n) {
    int i, j;
    List(STEntry*) possibilities = NULL;
    List(Type*) expected;
    Node* id = n->sons[0];

    bassert(id->e && id->e->overload, "attempt to resolve non-overloaded node");

    for (i=0; i<list_len(n->sons); ++i)
        if (n->sons[i]->type == anytype->override) {
            if (id->type) type_decref(id->type);
            id->type = anytype->override;
            type_incref(id->type);
            return;
        }

    for (i=0; i<2; ++i) {
        List(STEntry*) choices = possibilities ? possibilities : id->e->overloads;
        List(STEntry*) result = NULL;
        for (j=0; j<list_len(choices); ++j)
            if (funmatch(Mnothing, choices[j]->n, n, &expected, i))
                list_append(result, choices[j]);
        list_free(possibilities);
        possibilities = result;
        if (list_len(possibilities) <= 1) break;
    }

    if (list_len(possibilities) != 1) {
        String* s = id->s;
        if (!s) s = id->e->overloads[0]->n->parent->kind == Nstruct ?
                    id->e->overloads[0]->n->parent->s :
                    id->e->overloads[0]->n->s;
        if (!possibilities)
            error(id->loc, "no overload of '%s' with given arguments available",
                  s->str);
        else error(id->loc, "ambiguous occurrence of '%s'", s->str);
        for (i=0; i<list_len(id->e->overloads); ++i)
            funmatch(Mnote, id->e->overloads[i]->n, n, &expected, 0);
        id->type = anytype->override;
        type_incref(id->type);
    } else {
        id->e = possibilities[0];
        if (id->type) type_decref(id->type);
        id->type = possibilities[0]->n->type;
        type_incref(id->type);
    }

    list_free(possibilities);
}

static void check_index_magic(Node* n, Magic m) {
    int i;
    if (n->magic[m])
        for (i=0; i<list_len(n->magic[m]->overloads); ++i) {
            Node* nn;
            const char* strings[] = {[Mindex] = "[]", [Maindex] = "&[]"};
            nn = n->magic[m]->overloads[i]->n;
            bassert(nn, "builtin entry inside overloaded index");

            if (!nn->sons[0]) {
                error(nn->loc, "%s must return a value", strings[m]);
                nn->type->sons[0] = anytype->override;
                type_incref(nn->type->sons[0]);
            }

            if (m == Maindex && nn->type->sons[0] != anytype->override &&
                nn->type->sons[0]->kind != Tptr) {
                error(nn->sons[0]->loc, "&[] must return a pointer type");
                nn->type->sons[0] = anytype->override;
                type_incref(nn->type->sons[0]);
            }

            if (list_len(nn->type->sons) != 2)
                error(nn->loc, "%s function must take one argument", strings[m]);
            }
}

static void type_tv(Node* n) {
    int i;
    Node* tv;
    if (!n->tv) return;
    for (i=0; i<list_len(n->tv); ++i) {
        tv = n->tv[i];
        tv->type = new(Type);
        tv->type->kind = Tvar;
        tv->type->name = string_clone(tv->s);
        tv->flags |= Ftype;
    }
}

void type(Node* n) {
    Node* nn;
    Type* tt;
    int i;

    bassert(n, "expected non-null node");
    if (n->type) return;
    else if (n->typing) {
        error(n->loc, "type is recursive");
        n->type = anytype->override;
        n->typing = 0;
        return;
    } else n->typing = 1;

    if (n->this && n->kind != Nstruct) {
        if (n->bind) {
            type(n->bind);
            force_type_context(n->bind);
            bassert(n->bind->kind == Nid, "node bound to %d, not Nid",
                    n->bind->kind);
            bassert(n->bind->e && n->bind->e->n,
                    "node bound without bound entry");
            nn = n->bind->e->n;
        } else nn = n->parent;
        bassert(nn->kind == Nstruct, "non-Nstruct son with this");
        n->this->type = nn->type;
        type_incref(n->this->type);
    }

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

        for (i=0; i<Mend; ++i) {
            STEntry* e;
            String* s = string_new(magic_strings[i]);
            e = symtab_findl(n->tab, s);
            string_free(s);

            if (!e) continue;
            else if (e->overload) n->magic[i] = e;
            else error(e->n->loc, "magic item names must be functions");
        }

        n->flags |= Ftype;
        n->typing = 0;
        type_tv(n);
        for (i=0; i<list_len(n->sons); ++i) type(n->sons[i]);

        if (n->magic[Mnew])
            for (i=0; i<list_len(n->magic[Mnew]->overloads); ++i) {
                nn = n->magic[Mnew]->overloads[i]->n;
                bassert(nn, "builtin entry inside overloaded constructor");

                if (nn->type->sons[0])
                    error(nn->sons[0]->loc, "constructor cannot return a value");
            }
        else error(n->loc, "struct must have a constructor");

        for (i=Mindex; i<=Maindex; i++) check_index_magic(n, i);

        // XXX: These all assume one overload. Nfun needs to check for this!
        if (n->magic[Mdelete] && (nn = n->magic[Mdelete]->overloads[0]->n)
            && nn->type->sons[0])
            error(nn->sons[0]->loc, "destructor cannot return a value");

        if (n->magic[Mcopy] && (nn = n->magic[Mcopy]->overloads[0]->n)) {
            if (list_len(nn->type->sons) != 1)
                error(nn->loc, "copy constructor cannot take any arguments");

            if (nn->type->sons[0] != n->type)
                    error(nn->sons[0] ? nn->sons[0]->loc : nn->loc,
                          "copy constructor must return the parent's type");
        }

        if (n->magic[Mbool] && (nn = n->magic[Mbool]->overloads[0]->n)) {
            if (list_len(nn->type->sons) != 1)
                error(nn->loc, "bool converter cannot take any arguments");

            if (!nn->type->sons[0] ||
                !typematch(nn->type->sons[0], builtin_types[Tbool]->override,
                           NULL))
                error(nn->sons[0] ? nn->sons[0]->loc : nn->loc,
                      "bool converter must return bool");
        }
        break;
    case Nfun:
        if (n->sons[0]) {
            type(n->sons[0]);
            force_type_context(n->sons[0]);
        }
        if (n->sons[1]) type(n->sons[1]);
        n->type = new(Type);
        n->type->kind = Tfun;
        type_incref(n->type);
        if (n->sons[0]) {
            list_append(n->type->sons, n->sons[0]->type);
            type_incref(n->sons[0]->type);
        } else list_append(n->type->sons, NULL);
        if (n->sons[1]) for (i=0; i<list_len(n->sons[1]->sons); ++i) {
            list_append(n->type->sons, n->sons[1]->sons[i]->type);
            type_incref(n->sons[1]->sons[i]->type);
        }
        if (!n->import) type(n->sons[2]);

        if (IS_MAIN(n) && !(
            // This LONG condition just makes sure the given main is valid.
            list_len(n->type->sons) == 1
            && n->type->sons[0]
            && typematch(n->type->sons[0],
                         builtin_types[Tint]->override, NULL)))
            error(n->loc, "invalid main signature");
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
        nn = n->func;
        bassert(nn, "return without function");
        if (n->sons) {
            type(n->sons[0]);
            force_typed_expr_context(n->sons[0]);
        }
        if (nn->sons[0]) {
            Type* ret = nn->type->sons[0], *given;
            if (!n->sons) {
                error(n->loc, "function is supposed to return a value");
                note(nn->sons[0]->loc, "function return type declared here");
                break;
            }
            given = n->sons[0]->type;
            if (!typematch(ret, given, n->sons[0])) {
                String* rets, *givens;
                rets = typestring(ret);
                givens = typestring(given);
                error(n->sons[0]->loc, "function was declared to return type '%s'"
                                       ", not '%s'", rets->str, givens->str);
                note(nn->sons[0]->loc, "function return type declared here");
                declared_here(n->sons[0]);
                string_free(rets);
                string_free(givens);
            }
        } else if (n->sons) {
            error(n->sons[0]->loc, "function should not return a value");
            note(nn->loc, "function declared here");
        }
        break;
    case Nif:
    case Nwhile:
        type(n->sons[0]);
        type(n->sons[1]);
        if (!typematch(builtin_types[Tbool]->override, n->sons[0]->type,
                       n->sons[0])) {
            if (n->sons[0]->type->kind == Tbuiltin) {
                nn = new(Node);
                nn->kind = Ncast;
                list_append(nn->sons, n->sons[0]);
                list_append(nn->sons, new(Node));
                nn->sons[1]->kind = Nid;
                nn->sons[1]->s = string_newz("bool", 4);
                nn->sons[1]->e = builtin_types[Tbool];
                nn->sons[1]->parent = nn;
                nn->parent = n;
                type(nn);
                n->sons[0] = nn;
            } else if (n->sons[0]->type->kind == Tstruct &&
                       n->sons[0]->type->n->magic[Mbool]) {
                nn = new(Node);
                nn->kind = Ncall;
                list_append(nn->sons, new(Node));
                nn->sons[0]->kind = Nattr;
                list_append(nn->sons[0]->sons, n->sons[0]);
                nn->sons[0]->s = string_newz("bool", 4);
                nn->sons[0]->parent = nn;
                nn->parent = n;
                type(nn);
                n->sons[0] = nn;
            } else {
                String* ts = typestring(n->sons[0]->type);
                error(n->sons[0]->loc, "%s statement condition must be of a "
                                       "type convertible to 'bool'; '%s' isn't",
                      n->kind == Nif ? "if" : "while", ts->str);
                string_free(ts);
            }
        }
        break;
    case Ntypeof:
        type(n->sons[0]);
        if (!n->sons[0]->type) {
            error(n->sons[0]->loc, "type of expression is recursive");
            declared_here(n->sons[0]);
            n->type = anytype->override;
        } else {
            force_typed_expr_context(n->sons[0]);
            n->type = n->sons[0]->type;
        }
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
        force_typed_expr_context(n->sons[0]);
        force_typed_expr_context(n->sons[1]);
        if (n->sons[0]->type->kind == Tany) n->type = anytype->override;
        else if (n->sons[0]->type->kind == Tptr) {
            if (n->sons[1]->type->kind != Tbuiltin ||
                n->sons[1]->type->bkind == Tbool) {
                String* ts = typestring(n->sons[1]->type);
                error(n->loc, "only integral types can be indices of pointers, "
                              " not '%s'", ts->str);
                string_free(ts);
                n->type = anytype->override;
            } else {
                n->type = n->sons[0]->type->sons[0];
                if (n->sons[0]->type->mut) n->flags |= Fmv;
            }
        } else if (n->sons[0]->type->kind == Tstruct) {
            nn = n->sons[0];
            if (!nn->type->n->magic[Mindex] && !nn->type->n->magic[Maindex]) {
                String* ts = typestring(nn->type);
                error(n->loc, "structural type '%s' doesn't overload any index "
                              "operators", ts->str);
                string_free(ts);
                n->type = anytype->override;
            } else {
                n->kind = Ncall;
                n->sons[0] = new(Node);
                n->sons[0]->kind = Nid;
                n->sons[0]->loc = nn->loc;
                if ((n->parent->kind == Naddr || n->parent->kind == Nassign ||
                     !nn->type->n->magic[Mindex]) &&
                    nn->type->n->magic[Maindex]) {
                    n->sons[0]->e = nn->type->n->magic[Maindex];
                    i = 1;
                } else {
                    n->flags &= ~Faddr; // Remove Faddr.
                    n->sons[0]->e = nn->type->n->magic[Mindex];
                    i = 0;
                }
                type(n->sons[0]);
                resolve_overload(n);
                n->type = n->sons[0]->type == anytype->override ?
                          anytype->override : n->sons[0]->type->sons[0];
                if (i && n->type != anytype->override) {
                    bassert(n->type->kind == Tptr, "&[] return type is %d",
                            n->type->kind);
                    if (n->type->mut) n->flags |= Fmv;
                    // XXX: This lies about the type to the code generator.
                    n->type = n->type->sons[0];
                }
                // igen will later need the node that was indexed.
                list_append(n->sons, nn);
            }
        } else {
            String* ts = typestring(n->sons[0]->type);
            error(n->loc, "type '%s' cannot be indexed", ts->str);
            string_free(ts);
            n->type = anytype->override;
            n->flags |= Fmv;
        }
        type_incref(n->type);
        break;
    case Ninst:
        for (i=0; i<list_len(n->sons); ++i) {
            type(n->sons[i]);
            force_type_context(n->sons[i]);
        }
        n->flags |= Ftype;
        if (n->sons[0]->type == anytype->override) n->type = anytype->override;
        else if (n->sons[0]->type->kind != Tstruct ||
                 !n->sons[0]->type->n->tv) {
            String* ts = typestring(n->sons[0]->type);
            error(n->sons[0]->loc, "cannot instantiate non-generic type '%s'",
                  ts->str);
            declared_here(n->sons[0]);
            string_free(ts);
            n->type = anytype->override;
        } else if (list_len(n->sons)-1 != list_len(n->sons[0]->type->n->tv)) {
            String* ts = typestring(n->sons[0]->type);
            error(n->sons[0]->loc, "generic type '%s' expects %d type arguments"
                                   ", not %d", ts->str,
                                   list_len(n->sons[0]->type->n->tv),
                                   list_len(n->sons)-1);
            declared_here(n->sons[0]);
            string_free(ts);
            n->type = anytype->override;
        } else {
            n->type = new(Type);
            n->type->kind = Tinst;
            n->type->base = n->sons[0]->type;
            type_incref(n->type->base);
            for (i=1; i<list_len(n->sons); ++i) {
                list_append(n->type->sons, n->sons[i]->type);
                type_incref(n->sons[i]->type);
            }
        }
        type_incref(n->type);
        break;
    case Nnew: case Ncall:
        for (i=0; i<list_len(n->sons); ++i) {
            type(n->sons[i]);
            if (n->kind == Nnew && i == 0) force_type_context(n->sons[i]);
            else force_typed_expr_context(n->sons[i]);
        }
        tt = NULL;

        if (n->kind == Nnew) {
            Type* skt = skip(n->sons[0]->type);
            bassert(n->sons[0]->flags & Ftype, "new of non-type");

            if (skt->kind != Tstruct) {
                nn = NULL;
                tt = NULL;
                if (skt != anytype->override)
                    error(n->sons[0]->loc, "new requires a user-defined type");
                type_decref(n->sons[0]->type);
                n->sons[0]->type = anytype->override;
                type_incref(n->sons[0]->type);
            } else {
                nn = skt->n;
                if (!(n->sons[0]->e = nn->magic[Mnew])) {
                    type_decref(n->sons[0]->type);
                    n->sons[0]->type = anytype->override;
                    type_incref(n->sons[0]->type);
                }
                tt = n->sons[0]->type;
                type_incref(tt);
            }
        }

        if (n->sons[0]->e && n->sons[0]->type != anytype->override &&
            (n->kind == Nnew || (!n->sons[0]->type && n->sons[0]->e->overload))) {
            resolve_overload(n);

            if (n->sons[0]->kind == Nattr) n->sons[0]->attr = n->sons[0]->e->n;
        }

        if (n->sons[0]->type == anytype->override)
            n->type = n->kind == Nnew && tt ? tt : anytype->override;
        else if (n->kind == Ncall && !is_callable(n->sons[0])) {
            String* ts = typestring(n->sons[0]->type);
            error(n->loc, "cannot call non-callable type '%s'", ts->str);
            string_free(ts);
            declared_here(n->sons[0]);
            n->type = anytype->override;
        } else {
            List(Type*) expected;
            if (funmatch(Merror, n->sons[0], n, &expected, 0)) {
                if (expected && expected[0]) n->type = expected[0];
                else if (n->kind == Nnew) {
                    bassert(n->sons[0]->flags & Ftype,
                            "expected type as first son");
                    n->type = tt;
                } else {
                    n->type = anytype->override;
                    n->flags |= Fvoid;
                }
            } else n->type = anytype->override;
        }
        type_incref(n->type);
        if (tt) type_decref(tt);
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
            } else if (e->overload) {
                if (n->parent->kind == Ncall) {
                    n->e = e;
                    for (i=0; i<list_len(n->e->overloads); ++i)
                        type(n->e->overloads[i]->n);
                    n->type = NULL;
                } else {
                    error(n->loc, "cannot resolve overloaded method");
                    n->type = anytype->override;
                }
            } else {
                int flags = 0;

                bassert(e->n, "attribute node has no corresponding entry");
                type(e->n);
                n->type = e->n->type;
                n->attr = e->n;
                if (n->sons[0]->flags & Fmv) flags |= Fvar;
                flags &= e->n->flags & Fmv;
                n->flags |= flags;
            }
        }
        if (n->type) type_incref(n->type);
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
            if (n->sons[0]->type->kind == Tany || n->sons[1]->type->kind == Tany)
                n->type = anytype->override;
            else if (n->sons[0]->type->kind != Tbuiltin ||
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
            if (n->e->overload) {
                int i;
                for (i=0; i<list_len(n->e->overloads); ++i)
                    type(n->e->overloads[i]->n);
            } else if (n->e->n) {
                type(n->e->n);
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
    case Nstr:
        n->type = builtins[Bstr]->type;
        type_incref(n->type);
        break;
    case Nsons: fatal("unexpected node kind Nsons");
    }

    n->typing = 0;
}
