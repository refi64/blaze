/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

#define SVFLAGS (Fmut | Fvar | Fcst)

static void make_magic_this(Node* n) {
    STEntry* e;
    String* s = string_new("@");

    n->this = new(Node);
    n->this->kind = Nid;
    n->this->module = n->module;

    e = stentry_new(n->this, s, NULL);
    symtab_add(n->tab, s, e);
    string_free(s);
}

static void resolve0(Node* n) {
    STEntry* e;
    int i;
    const char* cs;
    bassert(n && (n->kind == Nmodule || n->parent), "node has no parent");
    if (n->kind != Nmodule && n->kind != Nfun && !n->tab) n->tab = n->parent->tab;
    if (n->parent && n->parent->module) n->module = n->parent->module;
    switch (n->kind) {
    case Nmodule:
        n->tab = symtab_new();
        if (strcmp(n->s->str, BUILTINS) == 0) n->tab->level = 0;

        for (i=0; i<list_len(n->sons); ++i) {
            n->sons[i]->parent = n->sons[i]->module = n;
            resolve0(n->sons[i]);
            if (n->sons[i]->s->str[0] != '_') n->sons[i]->export = 1;
        }

        if (strcmp(n->s->str, BUILTINS) == 0) {
            String* s;
            #define B(x) \
                s = string_new(#x);\
                builtins[B##x] = symtab_findl(n->tab, s)->n;\
                string_free(s);
            B(str)
            B(true)
            B(false)
            #undef B
        }
        break;
    case Nstruct:
        e = stentry_new(n, n->s, NULL);
        n->tab = symtab_sub(n->parent->tab);
        symtab_add(n->parent->tab, n->s, e);

        n->tab->level = -n->tab->level;

        make_magic_this(n);

        for (i=0; i<list_len(n->sons); ++i) {
            n->sons[i]->parent = n;
            n->sons[i]->flags |= Fmemb;
            resolve0(n->sons[i]);
        }
        break;
    case Nfun:
        n->tab = symtab_sub(n->parent->tab);
        cs = strchr(n->s->str, '.');
        if (cs) {
            String* fs;

            n->bind = new(Node);
            n->bind->kind = Nid;
            n->bind->parent = n;
            n->bind->module = n->module;
            n->bind->tab = n->tab;
            n->bind->s = string_newz(n->s->str, cs-n->s->str);
            n->bind->loc = n->loc;

            n->flags |= Fmemb;

            fs = string_new(cs+1);
            string_free(n->s);
            n->s = fs;
        } else {
            e = stentry_new_overload(n, n->s);
            symtab_add(n->parent->tab, n->s, e);
        }

        if (n->parent->kind == Nstruct || cs) {
            make_magic_this(n); // Overrides the parent struct's this.
            if (n->flags & Fmvm) n->this->flags |= Fmv;
        }

        for (i=0; i<list_len(n->sons); ++i) {
            if (!n->sons[i]) continue;
            n->sons[i]->parent = n;
            n->sons[i]->func = n;
            resolve0(n->sons[i]);
        }
        break;
    case Narglist:
        for (i=0; i<list_len(n->sons); ++i) {
            bassert(n->sons[i]->kind == Ndecl, "Narglist has son of kind %d",
                    n->sons[i]->kind);
            n->sons[i]->parent = n;
            resolve0(n->sons[i]);
        }
        break;
    case Ndecl:
        n->sons[0]->parent = n;
        resolve0(n->sons[0]);
        e = stentry_new(n, n->s, NULL);
        symtab_add(n->tab, n->s, e);
        if (n->sons[1]) {
            n->sons[1]->parent = n;
            resolve0(n->sons[1]);
        }
        break;
    case Nbody:
        for (i=0; i<list_len(n->sons); ++i) {
            n->tab = symtab_sub(n->tab);
            n->sons[i]->parent = n;
            n->sons[i]->func = n->func;
            resolve0(n->sons[i]);
        }
        break;
    case Nlet:
        n->sons[0]->parent = n;
        n->sons[0]->tab = symtab_sub(n->tab->parent);
        n->sons[0]->tab->isol = n->tab;
        resolve0(n->sons[0]);
        e = stentry_new(n, n->s, NULL);
        symtab_add(n->tab, n->s, e);
        break;
    case Nassign:
        n->sons[0]->parent = n->sons[1]->parent = n;
        resolve0(n->sons[0]);
        resolve0(n->sons[1]);
        break;
    case Nreturn:
        if (n->sons) {
            n->sons[0]->parent = n;
            resolve0(n->sons[0]);
        }
        break;
    case Nif:
    case Nwhile:
        n->sons[1]->func = n->func;
        n->sons[0]->parent = n->sons[1]->parent = n;
        resolve0(n->sons[0]);
        resolve0(n->sons[1]);
        break;
    case Ntypeof: case Nptr: case Nderef: case Naddr:
        n->sons[0]->parent = n;
        resolve0(n->sons[0]);
        break;
    case Nnew: case Ncall: case Nindex: case Ncast: case Nop:
        for (i=0; i<list_len(n->sons); ++i) {
            n->sons[i]->parent = n;
            resolve0(n->sons[i]);
        }
        break;
    case Nattr:
        n->sons[0]->parent = n;
        resolve0(n->sons[0]);
        break;
    case Nid: case Nint: case Nstr: break;
    case Nsons: fatal("unexpected node kind Nsons");
    }
}

#define OVERLOAD(x) ((x)->kind == Ncall || (x)->kind == Nnew)

static void resolve1(Node* n) {
    int i;
    bassert(n, "expected non-null node");
    switch (n->kind) {
    case Nfun:
        if (n->bind) {
            resolve1(n->bind);
            if (n->bind->e && n->bind->e->n && n->bind->e->n->kind == Nstruct) {
                STEntry* e = stentry_new_overload(n, n->s);
                symtab_add(n->bind->e->n->tab, n->s, e);
            }
        }
    // Fallthrough.
    case Nmodule: case Nstruct: case Narglist: case Ndecl: case Ncast: case Nif:
    case Nwhile:
        for (i=0; i<list_len(n->sons); ++i)
            if (n->sons[i]) resolve1(n->sons[i]);
        break;
    case Nbody:
        for (i=0; i<list_len(n->sons); ++i) resolve1(n->sons[i]);
        break;
    case Nreturn:
        if (n->sons) resolve1(n->sons[0]);
        break;
    case Nlet: resolve1(n->sons[0]); break;
    case Nassign: case Nindex: case Nop:
        resolve1(n->sons[0]);
        resolve1(n->sons[1]);
        break;
    case Ntypeof: case Nptr: case Nderef:
        resolve1(n->sons[0]);
        break;
    case Naddr:
        resolve1(n->sons[0]);
        if (!(n->sons[0]->flags & Faddr)) {
            error(n->sons[0]->loc, "expression must be addressable");
            declared_here(n->sons[0]);
        }
        break;
    case Nnew: case Ncall:
        for (i=0; i<list_len(n->sons); ++i) resolve1(n->sons[i]);
        break;
    case Nattr:
        resolve1(n->sons[0]);
        n->flags |= n->sons[0]->flags & SVFLAGS;
        break;
    case Nid:
        if (!(n->e = symtab_finds(n->tab, n->s))) {
            STEntry* e;
            if (n->tab->isol && (e = symtab_finds(n->tab->isol, n->s))) {
                error(n->loc, "identifier '%s' cannot reference itself in its own"
                              " declaration", n->s->str);
                note(e->n->loc, "'%s' declared here", n->s->str);
            }
            else error(n->loc, "undeclared identifier '%s'", n->s->str);
            return;
        }

        if (n->e->overload && !OVERLOAD(n->parent)) {
            if (list_len(n->e->overloads) > 1) {
                int i;
                error(n->loc, "ambiguous occurrence of '%s'", n->s->str);
                for (i=0; i<list_len(n->e->overloads); ++i) {
                    bassert(n->e->overloads[i]->n,
                            "'%s' is overloaded with a compiler builtin",
                            n->s->str);
                    declared_here(n->e->overloads[i]->n);
                }
            }
            n->e = n->e->overloads[0];
        } else if (n->e->overload && list_len(n->e->overloads) == 1)
            n->e = n->e->overloads[0];

        if (n->e->level < 0) {
            error(n->loc, "identifier '%s' cannot be accessed without @",
                  n->s->str);
            if (n->e->n) declared_here(n->e->n);
        } else if (!n->e->overload && n->e->n) {
            bassert(n->module && n->e->n->module, "node has no module");
            if (!n->e->n->export && n->e->n->module != n->module) {
                error(n->loc, "attempt to access unexported identifier '%s'",
                      n->s->str);
                declared_here(n->e->n);
            }
            n->flags |= n->e->n->flags & SVFLAGS;
            n->e->n->flags |= Fused;
            n->flags |= Fused;
        }
        break;
    case Nint: case Nstr: break;
    case Nsons: fatal("unexpected node kind Nsons");
    }
}

void resolve(Node* n) {
    resolve0(n);
    resolve1(n);
}
