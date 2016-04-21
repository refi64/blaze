/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

// "Deletes" the given IR.
static void nir(Instr* ir) {
    int i;
    ir->kind = Inull;
    if (ir->dst && ir->dst->uses) --ir->dst->uses;
    for (i=0; i<list_len(ir->v); ++i) --ir->v[i]->uses;
}

static int remove_unused_vars(Decl* d) {
    int i, j, removed=0;
    for (i=0; i<list_len(d->vars); ++i)
        if (d->vars[i]->uses == 0 && d->vars[i]->type &&
            !(d->vars[i]->ir && d->vars[i]->ir->kind == Iconstr) &&
            !(d->vars[i]->ir && d->vars[i]->ir->kind == Icall &&
              d->vars[i]->ir->v[0]->owner->ra)) {
            // This will cause code generators to not emit space for the variable.
            d->vars[i]->type = NULL;
            for (j=0; j<list_len(d->vars[i]->destr); ++j)
                nir(d->vars[i]->destr[j]);
            ++removed;

            if (d->vars[i]->ir && d->vars[i]->ir->flags & Fpure)
                nir(d->vars[i]->ir);
        }

    return removed;
}

static void remove_useless_news(Decl* d) {
    int i, j;
    for (i=0; i<list_len(d->sons); ++i) {
        Instr* ir = d->sons[i];
        if (ir->kind == Inew && ir->v[0]->uses == (list_len(ir->v[0]->destr)+1) &&
            !ir->v[0]->name && ir->v[0]->ir && ir->v[0]->ir != &magic) {
            ir->v[0]->ir->dst = ir->dst;
            ir->dst->ir = ir->v[0]->ir;
            ir->v[0]->type = NULL;
            for (j=0; j<list_len(ir->v[0]->destr); ++j) nir(ir->v[0]->destr[j]);
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
