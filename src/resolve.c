#include "blaze.h"

static void resolve0(Node* n) {
    STEntry* e;
    int i;
    assert(n && (n->kind == Nmodule || n->parent));
    if (n->kind != Nmodule && n->kind != Nfun && !n->tab) n->tab = n->parent->tab;
    switch (n->kind) {
    case Nmodule:
        n->tab = symtab_new();
        for (i=0; i<list_len(n->sons); ++i) {
            n->sons[i]->parent = n;
            resolve0(n->sons[i]);
        }
        break;
    case Nstruct:
        e = stentry_new(n, n->s, NULL);
        n->tab = symtab_sub(n->parent->tab);
        symtab_add(n->parent->tab, n->s, e);

        n->tab->level = -n->tab->level;

        for (i=0; i<list_len(n->sons); ++i) {
            n->sons[i]->parent = n;
            n->sons[i]->flags |= Fmemb;
            resolve0(n->sons[i]);
        }
        break;
    case Nfun:
        e = stentry_new(n, n->s, NULL);
        symtab_add(n->parent->tab, n->s, e);
        n->tab = symtab_sub(n->parent->tab);

        if (strcmp(n->loc.module, "__main__") == 0 &&
            strcmp(n->s->str, "main") == 0)
            n->exportc = string_new("main");

    // Fall though.
    case Nconstr:
        for (i=0; i<list_len(n->sons); ++i) {
            if (!n->sons[i]) continue;
            n->sons[i]->parent = n;
            n->sons[i]->func = n;
            resolve0(n->sons[i]);
        }
        break;
    case Narglist:
        for (i=0; i<list_len(n->sons); ++i) {
            assert(n->sons[i]->kind == Ndecl);
            n->sons[i]->parent = n;
            resolve0(n->sons[i]);
        }
        break;
    case Ndecl:
        n->sons[0]->parent = n;
        resolve0(n->sons[0]);
        e = stentry_new(n, n->s, NULL);
        symtab_add(n->tab, n->s, e);
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
    case Ntypeof: case Nptr: case Nderef: case Naddr:
        n->sons[0]->parent = n;
        resolve0(n->sons[0]);
        break;
    case Ncall:
        for (i=0; i<list_len(n->sons); ++i) {
            n->sons[i]->parent = n;
            resolve0(n->sons[i]);
        }
        break;
    case Nid: break;
    case Nint: break;
    case Nsons: assert(0);
    }
}

static void resolve1(Node* n) {
    int i;
    assert(n);
    switch (n->kind) {
    case Nmodule: case Nstruct: case Nconstr: case Nfun: case Narglist:
        for (i=0; i<list_len(n->sons); ++i)
            if (n->sons[i]) resolve1(n->sons[i]);
        break;
    case Ndecl:
        resolve1(n->sons[0]);
        break;
    case Nbody:
        for (i=0; i<list_len(n->sons); ++i) resolve1(n->sons[i]);
        break;
    case Nreturn:
        if (n->sons) resolve1(n->sons[0]);
        break;
    case Nlet: resolve1(n->sons[0]); break;
    case Nassign:
        resolve1(n->sons[0]);
        resolve1(n->sons[1]);
        if (n->kind == Nid && !n->e) break;
        if (n->sons[0]->flags & Fcst) {
            error(n->sons[0]->loc, "left-hand side of assignment cannot be "
                                   "constant");
            declared_here(n->sons[0]);
        } else if (!(n->sons[0]->flags & Faddr)) {
            error(n->sons[0]->loc, "left-hand side of assignment must be "
                                   "addressable");
            declared_here(n->sons[0]);
        } else if (!(n->sons[0]->flags & Fvar)) {
            error(n->sons[0]->loc, "left-hand side of assignment must be "
                                   "variable");
            make_mutvar(declared_here(n->sons[0]), Fvar, n->sons[0]->flags);
        }
        break;
    case Ntypeof: case Nptr: case Nderef: resolve1(n->sons[0]); break;
    case Naddr:
        resolve1(n->sons[0]);
        if (!(n->sons[0]->flags & Faddr)) {
            error(n->sons[0]->loc, "expression must be addressable");
            declared_here(n->sons[0]);
        }
        break;
    case Ncall:
        for (i=0; i<list_len(n->sons); ++i) resolve1(n->sons[i]);
        break;
    case Nid:
        if (!(n->e = symtab_finds(n->tab, n->s))) {
            STEntry* e;
            if (n->tab->isol && (e = symtab_finds(n->tab->isol, n->s))) {
                error(n->loc, "identifier %s cannot reference itself in its own "
                              "declaration", n->s->str);
                note(e->n->loc, "%s declared here", n->s->str);
            }
            else error(n->loc, "undeclared identifier %s", n->s->str);
        } else if (n->e->level < 0) {
            error(n->loc, "identifier %s cannot be accessed without @",
                  n->s->str);
            if (n->e->n) declared_here(n->e->n);
        } else if (n->e->n) {
            n->flags |= (n->e->n->flags & Fmut) | (n->e->n->flags & Fvar) |
                        (n->e->n->flags & Fcst);
            n->e->n->flags |= Fused;
            n->flags |= Fused;
        }
        break;
    case Nint: break;
    case Nsons: assert(0);
    }
}

void resolve(Node* n) {
    resolve0(n);
    resolve1(n);
}
