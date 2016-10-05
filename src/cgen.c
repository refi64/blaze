/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "blaze.h"

const char* typenames[] = {"int", "unsigned char", "char", "unsigned long",
                           "int"};
int type_id=0;

#define CNAME(x) ((x)?(x)->d.cname->str:"void")

static void generate_basename(char p, GData* d, String* name, int id) {
    char buf[1024];
    if (d->cname) return;
    d->cname = string_newz(&p, 1);
    snprintf(buf, sizeof(buf), "%d", id);
    string_merges(d->cname, buf);
    if (name) {
        string_mergec(d->cname, '_');
        if (!strcmp(name->str, "[]")) string_merges(d->cname, "$index");
        else if (!strcmp(name->str, "&[]")) string_merges(d->cname, "$aindex");
        else string_merge(d->cname, name);
    }
}

static void generate_typename(Type* t) {
    if (t->kind == Tbuiltin) t->d.cname = string_new(typenames[t->bkind]);
    else if (t->kind == Tptr) {
        generate_typename(t->sons[0]);
        if (t->sons[0]->kind == Tvar) {
            if (t->sons[0]->sons && t->sons[0]->sons[0])
                t->d.cname = string_clone(t->sons[0]->sons[0]->d.cname);
            else
                t->d.cname = string_new("void");
        } else t->d.cname = string_clone(t->sons[0]->d.cname);
        string_mergec(t->d.cname, '*');
    } else generate_basename('t', &t->d, t->name, type_id++);
}

static void generate_argname(Var* v) {
    generate_basename('a', &v->d, v->name, v->id);
}

static void generate_varname(Var* v) {
    if (v->d.cname) return;

    if (v->deref) {
        String* s = string_newz("(*", 2);
        bassert(v->base, "var deref has null base");
        generate_varname(v->base);
        string_merge(s, v->base->d.cname);
        string_mergec(s, ')');
        v->d.cname = s;
    } else {
        if (v->base) {
            generate_varname(v->base);
            v->d.cname = string_clone(v->base->d.cname);
        } else generate_basename('v', &v->d, v->name, v->id);
        if (v->av) {
            Var* last = *v->av[list_len(v->av)-1];
            bassert(!v->iv, "attribute var also has indexes");
            if (last->flags & Fstc) {
                string_free(v->d.cname);
                v->d.cname = string_clone(last->d.cname);
                v->flags |= Fstc;
            } else {
                int i;
                for (i=0; i<list_len(v->av); ++i) {
                    string_mergec(v->d.cname, '.');
                    string_merge(v->d.cname, (*v->av[i])->d.cname);
                }
            }
        } else if (v->iv) {
            int i;
            for (i=0; i<list_len(v->iv); ++i) {
                generate_varname(v->iv[i]);
                string_mergec(v->d.cname, '[');
                string_merge(v->d.cname, v->iv[i]->d.cname);
                string_mergec(v->d.cname, ']');
            }
        }
    }
}

static void generate_declname(Decl* d) {
    static const char prefixes[] = "fg";

    if (d->import) d->v->d.cname = string_clone(d->import);
    else if (d->exportc) d->v->d.cname = string_clone(d->exportc);
    else generate_basename(prefixes[d->kind], &d->v->d, d->v->name, d->v->id);
}

static Type** skipptrm(Type** t) {
    while ((*t)->kind == Tptr) t = &(*t)->sons[0];
    return t;
}

static Type* skipptr(Type* t) { return *skipptrm(&t); }

static void cgen_typedef(Type* t, FILE* output) {
    int i;
    bassert(t, "expected non-null type");
    if ((t->d.put_typedef || t->d.done) && t->kind != Tptr) return;
    if (t->kind == Tinst && t->di) {
        t->d.cname = string_clone(t->di->d.cname);
        return;
    }
    generate_typename(t);
    for (i=0; i<list_len(t->sons); ++i)
        if (t->sons[i]) cgen_typedef(t->sons[i], output);
    switch (t->kind) {
    case Tany: fatal("unexpected type kind Tany");
    case Tbuiltin: case Tptr: break;
    case Tfun:
        // XXX: this is a hack.
        fprintf(output, "typedef %s", t->sons[0] && t->sons[0]->kind == Tvar ?
                                      "void*" : CNAME(t->sons[0]));
        fprintf(output, " (*%s)(", CNAME(t));
        for (i=1; i<list_len(t->sons); ++i) {
            if (i > 1) fputs(", ", output);
            // XXX: here too!
            fputs(t->sons[i] && t->sons[i]->kind == Tvar ? "void*" :
                  CNAME(t->sons[i]), output);
        }
        fputs(");\n", output);
        break;
    case Tstruct:
        if (t->n->tv)
            for (i=0; i<list_len(t->insts); ++i) {
                Type* g = t->insts[i];
                cgen_typedef(g, output);
                fprintf(output, "typedef struct %s %s;\n", CNAME(g), CNAME(g));
            }
        else
            fprintf(output, "typedef struct %s %s;\n", CNAME(t), CNAME(t));
        break;
    case Tinst:
        /* Prevent recursion errors, since cgen_typedef(Tstruct) calls
           cgen_typedef(Tinst). */
        if (!t->base->d.cname) cgen_typedef(t->base, output);
        break;
    case Tvar: break;
    }
    t->d.put_typedef = 1;
}

static void cgen_decl0(Decl* d, FILE* output, int external);

static void cgen_set_tv(List(String*)* orig_cnames, Type* t, Type* inst) {
    int i;
    Type* tvt;
    set_tv_context(t->n->tv, inst->sons);
    *orig_cnames = NULL;
    list_append(*orig_cnames, t->d.cname);
    t->d.cname = inst->d.cname;
    for (i=0; i<list_len(t->n->tv); ++i) {
        tvt = t->n->tv[i]->type;
        list_append(*orig_cnames, tvt->d.cname);
        tvt->d.cname = tvt->sons[0]->d.cname;
    }
}

static void cgen_clear_tv(List(String*)* orig_cnames, Type* t) {
    int i;
    clear_tv_context(t->n->tv);
    t->d.cname = (*orig_cnames)[0];
    for (i=0; i<list_len(t->n->tv); ++i)
        t->n->tv[i]->type->d.cname = (*orig_cnames)[i+1];
    list_free(*orig_cnames);
    *orig_cnames = NULL;
}

static void cgen_typeimpl(Type* t, FILE* output) {
    int i;

    if (t->kind != Tstruct || t->d.done) return;
    if (t->n->tv && !t->n->tv[0]->type->sons) {
        String* t_cname;
        List(String*) orig_cnames;
        t_cname = t->d.cname;
        for (i=0; i<list_len(t->insts); ++i) {
            cgen_set_tv(&orig_cnames, t, t->insts[i]);
            cgen_typeimpl(t, output);
            t->d.done = 0;
            cgen_clear_tv(&orig_cnames, t);
        }
        t->d.cname = t_cname;
    } else {
        fprintf(output, "struct %s {\n", CNAME(t));
        for (i=0; i<list_len(t->d.sons); ++i) {
            Decl* d = t->d.sons[i];
            if (d->kind != Dglobal) continue;
            fputs("    ", output);
            cgen_decl0(d, output, 0);
        }
        fputs("};\n", output);
    }
    t->d.done = 1;
}

#define HAS_COPY(v) ((v)->type && (v)->type->kind == Tstruct && \
                     (v)->type->n->magic[Mcopy])
#define RADDR(vr) (((vr)->av ? *(vr)->av[list_len((vr)->av)-1] : (vr)) \
                   ->owner->ra)

static const char* copy(Var* v) {
    return HAS_COPY(v) ? CNAME(v->type->n->magic[Mcopy]->overloads[0]->n->v) : "";
}

static const char* copy_addr(Var* v) {
    return HAS_COPY(v) ? "&" : "";
}

static void dfun_inst_cname(String* s, Type* inst) {
    string_mergec(s, '$');
    string_merge(s, inst->d.cname);
}

static void cgen_set(Var* dst, int dstaddr, Var* src, FILE* output) {
    if (HAS_COPY(src) && src->type->n->magic[Mcopy]->overloads[0]->n->d->ra)
        fprintf(output, "%s(%s%s, %s%s)", copy(src), dstaddr ? "" : "&",
                        CNAME(dst), copy_addr(src), CNAME(src));
    else
        fprintf(output, "%s%s = %s(%s%s)", dstaddr ? "*" : "", CNAME(dst),
                        copy(src), copy_addr(src), CNAME(src));
}

#define DFUN_THIS(d) ((d)->args[0]->type->sons[0])

static void cgen_ir(Decl* d, Instr* ir, FILE* output) {
    int i;
    List(String*) orig_cnames = NULL;
    for (i=0; i<list_len(ir->v); ++i) {
        Type* t = ir->v[i]->base && ir->v[i]->base->type ?
                  skipptr(ir->v[i]->base->type) : NULL;
        generate_varname(ir->v[i]);
        if (ir->v[i]->type &&
            ((ir->v[i]->flags & Fstc && t &&
              (t->kind == Tinst ||
               (d->flags & Fmemb && t == DFUN_THIS(d) && t->n->tv))) ||
             (ir->kind == Iconstr && ir->dst->type->kind == Tinst && i == 0))) {
            list_append(orig_cnames, string_clone(ir->v[i]->d.cname));
            dfun_inst_cname(ir->v[i]->d.cname, ir->kind == Iconstr ?
                                               ir->dst->type : t);
        } else list_append(orig_cnames, NULL);
    }
    // The IR was optimized out by iopt.
    if (ir->kind == Inull || (ir->kind == Iaddr && ir->dst->uses == 0) ||
        (ir->kind == Inew && !ir->dst->type)) return;

    fputs("    ", output);
    if (ir->dst && ir->dst->type && ir->kind != Iconstr && ir->kind != Inew &&
        ir->kind != Istr && !(ir->kind == Icall && RADDR(ir->v[0])))
        fprintf(output, "%s = ", CNAME(ir->dst));

    switch (ir->kind) {
    case Inull: fatal("unexpected ir kind Inull");
    case Isr:
        if (ir->v) {
            /* Only move local variables and rvalues (which should now be locals
               anyway). */
            int do_move = ir->v[0]->owner == d && !(ir->v[0]->flags & Farg);
            if (do_move)
                fprintf(output, "%s%s = %s", d->ra ? "*" : "", CNAME(d->rv),
                        CNAME(ir->v[0]));
            else cgen_set(d->rv, d->ra, ir->v[0], output);
            ir->v[0]->no_destr = do_move;
        }
        break;
    case Icjmp:
        fprintf(output, "if (!(%s)) goto L%d", CNAME(ir->v[0]), ir->label);
        break;
    case Ijmp:
        fprintf(output, "goto L%d", ir->label);
        break;
    case Ilabel:
        fprintf(output, "L%d:", ir->label);
        break;
    case Iset:
        cgen_set(ir->v[0], 0, ir->v[1], output);
        break;
    case Inew:
        cgen_set(ir->dst, 0, ir->v[0], output);
        break;
    case Idel:
        if (ir->v[1]->type && !ir->v[1]->no_destr)
            fprintf(output, "%s(&(%s));\n", CNAME(ir->v[0]), CNAME(ir->v[1]));
        break;
    case Iconstr:
        fprintf(output, "%s(&(%s)", CNAME(ir->v[0]), CNAME(ir->dst));
        for (i=1; i<list_len(ir->v); ++i)
            fprintf(output, ", %s", CNAME(ir->v[i]));
        fputc(')', output);
        break;
    case Icall:
        fprintf(output, "%s(", CNAME(ir->v[0]));

        if (RADDR(ir->v[0])) {
            fprintf(output, "&(%s)", CNAME(ir->dst));
            if (list_len(ir->v) > 1) fputs(", ", output);
        }

        if (ir->v[0]->flags & Fstc && ir->v[0]->base) {
            if (RADDR(ir->v[0]) && list_len(ir->v) <= 1) fputs(", ", output);
            fprintf(output, "&(%s", CNAME(ir->v[0]->base));
            for (i=0; i<list_len(ir->v[0]->av)-1; ++i)
                fprintf(output, ".%s", CNAME(*ir->v[0]->av[i]));
            fputc(')', output);
            if (list_len(ir->v) > 1) fputs(", ", output);
        }

        for (i=1; i<list_len(ir->v); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(ir->v[i]), output);
        }
        fputc(')', output);
        break;
    case Iaddr:
        if (ir->v[0]->deref) fprintf(output, "%s", CNAME(ir->v[0]->base));
        else fprintf(output, "&%s", CNAME(ir->v[0]));
        break;
    case Icast:
        bassert(ir->dst, "cast without destination");
        fprintf(output, "(%s)(%s)", CNAME(ir->dst->type), CNAME(ir->v[0]));
        break;
    case Iop:
        fprintf(output, "%s %s %s", CNAME(ir->v[0]), op_strings[ir->op],
                CNAME(ir->v[1]));
        break;
    case Iint:
        fputs(ir->s->str, output);
        break;
    case Istr:
        bassert(ir->dst->type == builtins[Bstr]->type,
                "string literal with non-string type");
        fprintf(output, "%s(&(%s), \"%s\", %zu)",
                CNAME(ir->dst->type->n->magic[Mnew]->overloads[0]->n->v),
                CNAME(ir->dst), ir->s->str, ir->s->len);
        break;
    }
    fputs(";\n", output);

    for (i=0; i<list_len(orig_cnames); ++i)
        if (orig_cnames[i]) {
            string_free(ir->v[i]->d.cname);
            ir->v[i]->d.cname = orig_cnames[i];
        }
}

static void cgen_proto(Decl* d, FILE* output) {
    int i;

    bassert(d->kind == Dfun, "unexpected decl kind %d", d->kind);
    if (!d->exportc && !d->import && !d->export) fputs("static ", output);
    if (d->v->type && d->v->type->sons[0])
        generate_typename(d->v->type->sons[0]);
    fprintf(output, "%s %s(", d->v->type && !d->ra ? CNAME(d->v->type->sons[0])
                                                   : "void",
            CNAME(d->v));
    if (d->ra) {
        generate_varname(d->rv);
        generate_typename(d->ret);
        fprintf(output, "%s %s", CNAME(d->rv->type), CNAME(d->rv));
    }
    for (i=0; i<list_len(d->args); ++i) {
        generate_argname(d->args[i]);
        generate_typename(d->args[i]->type);
        if (i || d->ra) fputs(", ", output);
        fprintf(output, "%s %s", CNAME(d->args[i]->type), CNAME(d->args[i]));
    }
    fputc(')', output);
}

static void cgen_decl0(Decl* d, FILE* output, int external) {
    static int ptv = 0;
    generate_declname(d);
    switch (d->kind) {
    case Dfun:
        if (d->flags & Fmemb && DFUN_THIS(d)->n->tv && !ptv) {
            int i;
            Type* this = DFUN_THIS(d);
            String* d_cname = string_clone(d->v->d.cname);
            List(String*) orig_cnames;
            for (i=0; i<list_len(this->insts); ++i) {
                Type* inst = this->insts[i];
                dfun_inst_cname(d->v->d.cname, inst);
                cgen_set_tv(&orig_cnames, this, inst);
                ptv = 1;
                cgen_decl0(d, output, external);
                ptv = 0;
                cgen_clear_tv(&orig_cnames, this);
                string_free(d->v->d.cname);
                d->v->d.cname = string_clone(d_cname);
            }
            string_free(d_cname);
        } else {
            cgen_proto(d, output);
            fputs(";\n", output);
        }
        break;
    case Dglobal:
        generate_typename(d->v->type);
        if (external || d->import) fputs("extern ", output);
        fprintf(output, "%s %s;\n", CNAME(d->v->type), CNAME(d->v));
        break;
    }

}

static Var* trace_inst(Var* v) {
    Var* r = NULL;
    if (v->type->kind == Tinst) return v;
    else if (!v->ir) return NULL;
    else switch (v->ir->kind) {
    case Icall: return trace_inst(v->ir->v[0]->base);
    case Iop:
        r = trace_inst(v->ir->v[0]);
        if (!r) r = trace_inst(v->ir->v[1]);
        return r;
    default: fatal("generics are broken");
    }
}

static void cgen_decl1(Decl* d, FILE* output) {
    int i;
    static int ptv = 0;
    if (d->kind != Dfun || d->import) return;
    if (d->flags & Fmemb && d->args[0]->type->sons[0]->n->tv && DFUN_THIS(d) &&
        !ptv) {
        List(String*) orig_cnames;
        Type* this = DFUN_THIS(d);
        String* d_cname = string_clone(d->v->d.cname);
        for (i=0; i<list_len(this->insts); ++i) {
            Type* inst = this->insts[i];
            dfun_inst_cname(d->v->d.cname, inst);
            cgen_set_tv(&orig_cnames, DFUN_THIS(d), inst);
            ptv = 1;
            cgen_decl1(d, output);
            ptv = 0;
            cgen_clear_tv(&orig_cnames, DFUN_THIS(d));
            d->v->d.cname = string_clone(d_cname);
        }
        string_free(d_cname);
        return;
    }
    cgen_proto(d, output);
    fputs(" {\n", output);
    for (i=0; i<list_len(d->vars); ++i) {
        Type** t;
        Var* v = d->vars[i];
        if (!v->type) continue;
        t = skipptrm(&v->type);
        if ((*t)->kind == Tvar) {
            Type* o = *t;
            int ctx;
            Var* iv;
            if (!o->sons) {
                ctx = 1;
                iv = trace_inst(v);
                generate_varname(iv);
                set_tv_context(iv->type->base->n->tv, iv->type->sons);
            } else ctx = 0;
            *t = o->sons[0];
            type_incref(*t);
            if (ctx) clear_tv_context(iv->type->base->n->tv);
        }
        generate_typename(v->type);
        generate_varname(v);
        fprintf(output, "    %s %s;\n", CNAME(v->type), CNAME(v));
    }
    if (d->rv) {
        generate_varname(d->rv);
        if (!d->ra) {
            generate_typename(d->ret);
            fprintf(output, "    %s %s;\n", CNAME(d->ret), CNAME(d->rv));
        }
    }
    for (i=0; i<list_len(d->sons); ++i) cgen_ir(d, d->sons[i], output);
    if (d->rv && !d->ra) fprintf(output, "    return %s;\n", CNAME(d->rv));
    else fputs("    return;\n", output);
    fputs("}\n\n", output);
}

#define FREE_CNAME(b) do {\
        String** p = &(b)->d.cname;\
        if (*p) string_free(*p);\
        *p = NULL;\
    } while (0)

static void free_decl_cnames(Decl* d) {
    int i;
    FREE_CNAME(d->v);
    for (i=0; i<list_len(d->args); ++i) FREE_CNAME(d->args[i]);
    for (i=0; i<list_len(d->vars); ++i) FREE_CNAME(d->vars[i]);
    for (i=0; i<list_len(d->mvars); ++i) FREE_CNAME(d->mvars[i]);
    if (d->rv) FREE_CNAME(d->rv);
}

static void free_type_cnames(Type* t) {
    int i;
    if (!t) return;
    FREE_CNAME(t);
    for (i=0; i<list_len(t->sons); ++i) free_type_cnames(t->sons[i]);
    if (t->kind == Tstruct)
        for (i=0; i<list_len(t->d.sons); ++i) free_decl_cnames(t->d.sons[i]);
}

static List(const char*) all_inits = NULL;

void cgen_free(Module* m) {
    int i;

    if (m == NULL) {
        list_free(all_inits);
        all_inits = NULL;
        return;
    }

    for (i=0; i<list_len(m->types); ++i) free_type_cnames(m->types[i]);
    for (i=0; i<list_len(m->decls); ++i) {
        if (m->decls[i]->flags & Fmemb) continue;
        free_decl_cnames(m->decls[i]);
    }
}

#undef FREE_CNAME

static void cgen_header(Module* m, FILE* output, int external) {
    int i;

    for (i=0; i<list_len(m->types); ++i)
        cgen_typedef(m->types[i], output);
    for (i=0; i<list_len(m->types); ++i) m->types[i]->d.put_typedef = 0;
    fputs("\n\n", output);

    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl0(m->decls[i], output, external);
    fputs("\n\n", output);

    for (i=0; i<list_len(m->types); ++i)
        cgen_typeimpl(m->types[i], output);
    fputs("\n\n", output);
}

void cgen(Module* m, FILE* output) {
    int i;

    if (!m->main) fputs("extern ", output);
    fputs("int __blaze_argc;\n", output);
    if (!m->main) fputs("extern ", output);
    fputs("char** __blaze_argv;\n", output);

    for (i=0; i<list_len(m->imports); ++i) cgen_header(m->imports[i], output, 1);

    cgen_header(m, output, 0);
    for (i=0; i<list_len(m->types); ++i) m->types[i]->d.done = 0;

    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl1(m->decls[i], output);

    list_append(all_inits, CNAME(m->init->v));

    if (m->main) {
        fputs("int main(int argc, char** argv) {\n", output);
        fputs("    __blaze_argc = argc;\n", output);
        fputs("    __blaze_argv = argv;\n", output);
        for (i=0; i<list_len(all_inits); ++i)
            fprintf(output, "    %s();\n", all_inits[i]);
        fprintf(output, "    return %s();\n", CNAME(m->main->v));
        fputs("}\n", output);
    }
}
