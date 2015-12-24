#include "blaze.h"

// DSHtab* tab, Symtab* parent

static uint32_t strhash(String* str) { return ds_strhash(str->str); }
static int streq(String* a, String* b) { return ds_streq(a->str, b->str); }

STEntry* anytype=NULL;
STEntry* builtin_types[Tbend];

void init_builtin_types() {
    String* name;
    STEntry* e;
    Type* t;
    int i;
    const char* names[] = {"int", "char"};
    const int bkinds[] = {Tint, Tchar};
    for (i=0; i<Tbend; ++i) {
        name = string_new(names[i]);
        t = new(Type);
        t->kind = Tbuiltin;
        t->name = name;
        t->bkind = bkinds[i];
        type_incref(t);
        e = stentry_new(NULL, name, t);
        builtin_types[bkinds[i]] = e;
    }

    assert(!anytype);
    t = new(Type);
    t->kind = Tany;
    t->name = string_new("_any");
    type_incref(t);
    anytype = stentry_new(NULL, t->name, t);
}

STEntry* stentry_new(Node* n, String* name, Type* override) {
    STEntry* e = new(STEntry);
    e->n = n;
    e->name = string_clone(name);
    e->override = override;
    return e;
}

void stentry_free(STEntry* e) {
    if (e->override && !e->n) return;
    string_free(e->name);
    free(e);
}

static void add_builtins(Symtab* tab) {
    int i;
    for (i=0; i<Tbend; ++i)
        symtab_add(tab, builtin_types[i]->name, builtin_types[i]);
}

Symtab* symtab_new() {
    Symtab* res = new(Symtab);
    res->parent = NULL;
    res->tab = ds_hnew((DSHashFn)strhash, (DSCmpFn)streq);
    add_builtins(res);
    res->level = 1;
    return res;
}

STEntry* symtab_find(Symtab* tab, const char* name) {
    STEntry* e;
    String* sname = string_new(name);
    e = symtab_finds(tab, sname);
    string_free(sname);
    return e;
}
STEntry* symtab_finds(Symtab* tab, String* name) {
    assert(name);

    while (tab) {
        STEntry* e = ds_hget(tab->tab, name);
        if (e) return e;
        tab = tab->parent;
    }
    return NULL;
}

void symtab_add(Symtab* tab, String* name, STEntry* e) {
    STEntry* p;
    e->level = tab->level;
    p = symtab_finds(tab, name);
    if (p) {
        Location el = e->n->loc;
        if (p->level == e->level) {
            error(el, "duplicate definition of %s", name->str);
            note(p->n->loc, "previous definition is here");
            stentry_free(e);
            return;
        } else {
            if (p->level == 0)
                error(el, "redefinition of %s shadows builtin", name->str);
            else {
                warning(el, "redefinition of %s shadows outer definition",
                    name->str);
                note(p->n->loc, "previous definition is here");
                /* stentry_free(e); */
            }
        }
    }
    ds_hput(tab->tab, string_clone(name), e);
}

Symtab* symtab_sub(Symtab* tab) {
    Symtab* res = new(Symtab);
    res->parent = tab;
    res->tab = ds_hnew((DSHashFn)strhash, (DSCmpFn)streq);
    res->level = tab->level+1;
    list_append(tab->sons, res);
    return res;
}

void symtab_free(Symtab* tab) {
    int i, kc = ds_hcount(tab->tab);
    String** keys = (String**)ds_hkeys(tab->tab);
    STEntry** values = (STEntry**)ds_hvals(tab->tab);
    for (i=0; i<kc; ++i) {
        assert(keys[i]);
        STEntry* e = values[i];
        assert(e);
        stentry_free(e);
        string_free(keys[i]);
    }
    free(keys);
    free(values);
    ds_hfree(tab->tab);
    for (i=0; i<list_len(tab->sons); ++i) symtab_free(tab->sons[i]);
    list_free(tab->sons);
    free(tab);
}

void free_builtin_types(Symtab* tab) {
    STEntry* e;
    int i;
    for (i=0; i<Tbend; ++i) {
        e = builtin_types[i];
        type_decref(e->override);
        e->override = NULL;
        stentry_free(e);
    }

    type_decref(anytype->override);
    anytype->override = NULL;
    stentry_free(anytype);
}
