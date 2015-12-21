#include "blaze.h"

static void node_dump2(Node* n, int indent) {
    int i;
    assert(n);
    #define put(...) for (i=0; i<indent; ++i) printf("  "); printf(__VA_ARGS__);
    switch (n->kind) {
    case Nid: put("Nid (s:%s)", n->s->str); break;
    case Nint: put("Nint (s:%s)", n->s->str); break;
    case Nlet: put("Nlet (s:%s)", n->s->str); break;
    case Nassign: put("Nassign"); break;
    case Nreturn: put("Nreturn"); break;
    case Ntypeof: put("Ntypeof"); break;
    case Nfun: put("Nfun (s:%s)", n->s->str); break;
    case Narglist: put("Narglist"); break;
    case Narg: put("Narg (s:%s)", n->s->str); break;
    case Nbody: put("Nbody"); break;
    case Nmodule: put("Nmodule"); break;
    case Nsons: assert(0);
    }
    printf(" @ lines %d-%d, cols %d-%d", n->loc.first_line, n->loc.last_line,
           n->loc.first_column, n->loc.last_column);
    #define FLAG(f) if (n->flags & f) printf( ", " #f );
    FLAG(Ftype)
    FLAG(Faddr)
    FLAG(Fmut)
    FLAG(Fvar)
    FLAG(Fcst)
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
    assert(n);
    if (n->kind > Nsons) {
        for (i=0; i<list_len(n->sons); ++i)
            if (n->sons[i]) node_free(n->sons[i]);
        list_free(n->sons);
    }
    switch (n->kind) {
    case Nmodule:
        symtab_free(n->tab);
        break;
    case Nid: case Nint: case Nfun: case Nlet: case Narg:
        string_free(n->s);
        break;
    case Nsons: assert(0);
    default: break;
    }
    type_free(n->type, n);
    free(n);
}
