#include "blaze.h"

// "Deletes" the given IR.
static void nir(Instr* ir) {
    int i;
    ir->kind = Inull;
    if (ir->dst->uses) --ir->dst->uses;
    for (i=0; i<list_len(ir->v); ++i) --ir->v[i]->uses;
}

static int remove_unused_vars(Decl* d) {
    int i, removed=0;
    for (i=0; i<list_len(d->vars); ++i)
        if (d->vars[i]->uses == 0 && d->vars[i]->type) {
            // This will cause code generators to not emit space for the variable.
            d->vars[i]->type = NULL;
            ++removed;

            if (d->vars[i]->ir && d->vars[i]->ir->flags & Fpure)
                nir(d->vars[i]->ir);
        }

    return removed;
}

static void remove_useless_news(Decl* d) {
    int i;
    for (i=0; i<list_len(d->sons); ++i) {
        Instr* ir = d->sons[i];
        if (ir->kind == Inew && ir->v[0]->uses == 1 && !ir->v[0]->name &&
            ir->v[0]->ir) {
            ir->v[0]->ir->dst = ir->dst;
            ir->dst->ir = ir->v[0]->ir;
            ir->v[0]->type = NULL;
            nir(ir);
        }
    }
}

static void opt_decl(Decl* d) {
    if (d->kind != Dfun) return;

    remove_useless_news(d);
    while (remove_unused_vars(d));
}

void iopt(Module* m) {
    int i;
    for (i=0; i<list_len(m->decls); ++i) opt_decl(m->decls[i]);
}
