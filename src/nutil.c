/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

static void node_dump2(Node* n, int indent) {
    int i;
    bassert(n, "expected non-null node");
    #define put(...) for (i=0; i<indent; ++i) printf("  "); printf(__VA_ARGS__);
    switch (n->kind) {
    case Nid: put("Nid (s:%s)", n->s->str); break;
    case Nint: put("Nint (s:%s)", n->s->str); break;
    case Nstr: put("Nstr (s:%s)", n->s->str); break;
    case Nop: put("Nop (op:%s)", op_strings[n->op]); break;
    case Nderef: put("Nderef"); break;
    case Naddr: put("Naddr"); break;
    case Ncall: put("Ncall"); break;
    case Nindex: put("Nindex"); break;
    case Nattr: put("Nattr (s:%s)", n->s->str); break;
    case Nnew: put("Nnew"); break;
    case Ncast: put("Ncast"); break;
    case Nptr: put("Nptr"); break;
    case Nlet: put("Nlet (s:%s)", n->s->str); break;
    case Nassign: put("Nassign"); break;
    case Nreturn: put("Nreturn"); break;
    case Nif: put("Nif"); break;
    case Nwhile: put("Nwhile"); break;
    case Ntypeof: put("Ntypeof"); break;
    case Nstruct: put("Nstruct (s:%s)", n->s->str); break;
    case Nfun: put("Nfun (s:%s)", n->s->str); break;
    case Narglist: put("Narglist"); break;
    case Ndecl: put("Ndecl (s:%s)", n->s->str); break;
    case Nbody: put("Nbody"); break;
    case Nmodule: put("Nmodule %s\n", n->s->str); break;
    case Nsons: fatal("unexpected node kind Nsons");
    }
    if (n->export) printf(" exported");
    printf(" @ lines %d-%d, cols %d-%d", n->loc.first_line, n->loc.last_line,
           n->loc.first_column, n->loc.last_column);
    #define FLAG(f) if (n->flags & f) printf( ", " #f );
    FLAG(Ftype)
    FLAG(Faddr)
    FLAG(Fmut)
    FLAG(Fvar)
    FLAG(Fcst)
    FLAG(Fused)
    /* The && n->sons isn't necessary, but it prevents printing a colon if
       there are no child nodes. */
    if (n->kind > Nsons && n->sons) {
        printf(":\n");
        for (i=0; i<list_len(n->sons); ++i)
            if (n->sons[i]) node_dump2(n->sons[i], indent+1);
    } else putchar('\n');
}

void node_dump(Node* n) { node_dump2(n, 0); }

void node_free(Node* n) {
    int i;
    if (!n) return;
    if (n->kind > Nsons) {
        for (i=0; i<list_len(n->sons); ++i) node_free(n->sons[i]);
        list_free(n->sons);
    }
    switch (n->kind) {
    case Nmodule:
        if (n->tab) symtab_free(n->tab);
        // Fallthrough.
    case Nstruct: case Nid: case Nint: case Nstr: case Nfun: case Nlet:
    case Ndecl: case Nattr:
        if (n->s) string_free(n->s);
        if (n->import) string_free(n->import);
        if (n->kind == Nfun && n->exportc) string_free(n->exportc);
        break;
    case Nsons: fatal("unexpected node kind Nsons");
    default: break;
    }
    if (n->type) type_decref(n->type);
    node_free(n->this);
    free(n);
}
