#include "blaze.h"

static void remove_unused_vars(Decl* d) {
    int i;
    for (i=0; i<list_len(d->vars); ++i)
        if (d->vars[i]->uses == 0)
            // This will cause code generators to not emit space for the variable.
            d->vars[i]->type = NULL;
}

static void opt_decl(Decl* d) {
    remove_unused_vars(d);
}

void iopt(Module* m) {
    int i;
    for (i=0; i<list_len(m->decls); ++i) opt_decl(m->decls[i]);
}
