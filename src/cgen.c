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
        string_merge(d->cname, name);
    }
}

static void generate_typename(Type* t) {
    if (t->kind == Tbuiltin) t->d.cname = string_new(typenames[t->bkind]);
    else if (t->kind == Tptr) {
        generate_typename(t->sons[0]);
        t->d.cname = string_clone(t->sons[0]->d.cname);
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

static void cgen_typedef(Type* t, FILE* output) {
    int i;
    bassert(t, "expected non-null type");
    if (t->d.put_typedef || t->d.done) return;
    generate_typename(t);
    for (i=0; i<list_len(t->sons); ++i)
        if (t->sons[i]) cgen_typedef(t->sons[i], output);
    switch (t->kind) {
    case Tany: fatal("unexpected type kind Tany");
    case Tbuiltin: case Tptr: break;
    case Tfun:
        fprintf(output, "typedef %s", CNAME(t->sons[0]));
        fprintf(output, " (*%s)(", CNAME(t));
        for (i=1; i<list_len(t->sons); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(t->sons[i]), output);
        }
        fputs(");\n", output);
        break;
    case Tstruct:
        fprintf(output, "typedef struct %s %s;\n", CNAME(t), CNAME(t));
        break;
    }
    t->d.put_typedef = 1;
}

static void cgen_decl0(Decl* d, FILE* output, int external);

static void cgen_typeimpl(Type* t, FILE* output) {
    int i;

    if (t->kind != Tstruct || t->d.done) return;
    fprintf(output, "struct %s {\n", CNAME(t));
    for (i=0; i<list_len(t->d.sons); ++i) {
        Decl* d = t->d.sons[i];
        if (d->kind != Dglobal) continue;
        fputs("    ", output);
        cgen_decl0(d, output, 0);
    }
    fputs("};\n", output);
    t->d.done = 1;
}

#define HAS_COPY(v) ((v)->type && (v)->type->kind == Tstruct && \
                     (v)->type->n->magic[Mcopy])
#define RADDR(vr) ((vr)->owner->v == (vr) && (vr)->owner->ra)

static const char* copy(Var* v) {
    return HAS_COPY(v) ? CNAME(v->type->n->magic[Mcopy]->v) : "";
}

static const char* copy_addr(Var* v) {
    return HAS_COPY(v) ? "&" : "";
}

static void cgen_set(Var* dst, int dstaddr, Var* src, FILE* output) {
    if (HAS_COPY(src) && src->type->n->magic[Mcopy]->d->ra)
        fprintf(output, "%s(%s%s, %s%s)", copy(src), dstaddr ? "" : "&",
                        CNAME(dst), copy_addr(src), CNAME(src));
    else
        fprintf(output, "%s%s = %s(%s%s)", dstaddr ? "*" : "", CNAME(dst),
                        copy(src), copy_addr(src), CNAME(src));
}

static void cgen_ir(Decl* d, Instr* ir, FILE* output) {
    int i;
    for (i=0; i<list_len(ir->v); ++i) generate_varname(ir->v[i]);
    // The IR was optimized out by either iopt or cgen_decl1.
    if (ir->kind == Inull || (ir->kind == Iaddr && ir->dst->uses == 0) ||
        (ir->kind == Inew && !ir->dst->type)) return;

    fputs("    ", output);
    if (ir->dst && ir->dst->type && ir->kind != Iconstr && ir->kind != Inew &&
        !(ir->kind == Icall && RADDR(ir->v[0])))
        fprintf(output, "%s = ", CNAME(ir->dst));

    switch (ir->kind) {
    case Inull: fatal("unexpected ir kind Inull");
    case Iret:
        if (ir->v) {
            /* Only move local variables and rvalues (which should now be locals
               anyway). */
            int do_move = ir->v[0]->owner == d && !(ir->v[0]->flags & Farg);
            if (do_move)
                fprintf(output, "%s%s = %s", d->ra ? "*" : "", CNAME(d->rv),
                        CNAME(ir->v[0]));
            else cgen_set(d->rv, d->ra, ir->v[0], output);
            fputs("; goto R", output);
            ir->v[0]->no_destr = do_move;
        }
        else fputs("goto R", output);
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
        if (ir->v[0]->ir->kind == Iaddr)
            cgen_set(ir->v[0]->ir->v[0], 0, ir->v[1], output);
        else cgen_set(ir->v[0], 1, ir->v[1], output);
        break;
    case Inew:
        cgen_set(ir->dst, 0, ir->v[0], output);
        break;
    case Iconstr:
        fprintf(output, "%s(&(%s)", CNAME(ir->v[0]), CNAME(ir->dst));
        for (i=1; i<list_len(ir->v); ++i)
            fprintf(output, ", %s", CNAME(ir->v[i]));
        fputc(')', output);
        break;
    case Icall:
        fprintf(output, "%s(", CNAME(ir->v[0]));
        if (ir->v[0]->flags & Fstc && ir->v[0]->base) {
            fprintf(output, "&(%s", CNAME(ir->v[0]->base));
            for (i=0; i<list_len(ir->v[0]->av)-1; ++i)
                fprintf(output, ".%s", CNAME(*ir->v[0]->av[i]));
            fputc(')', output);
            if (list_len(ir->v) > 1) fputs(", ", output);
        }

        if (RADDR(ir->v[0])) {
            fprintf(output, "&(%s)", CNAME(ir->dst));
            if (list_len(ir->v) > 1) fputs(", ", output);
        }

        for (i=1; i<list_len(ir->v); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(ir->v[i]), output);
        }
        fputc(')', output);
        break;
    case Iaddr:
        if (ir->v[0]->deref) fputs(CNAME(ir->v[0]->base), output);
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
    }
    fputs(";\n", output);
}

static void cgen_proto(Decl* d, FILE* output) {
    int i;

    bassert(d->kind == Dfun, "unexpected decl kind %d", d->kind);
    if (!d->exportc && !d->import && !d->export) fputs("static ", output);
    fprintf(output, "%s %s(", d->v->type && !d->ra ? CNAME(d->v->type->sons[0])
                                                   : "void",
            CNAME(d->v));
    if (d->ra) {
        generate_varname(d->rv);
        fprintf(output, "%s* %s", CNAME(d->rv->type), CNAME(d->rv));
    }
    for (i=0; i<list_len(d->args); ++i) {
        generate_argname(d->args[i]);
        if (i || d->ra) fputs(", ", output);
        fprintf(output, "%s %s", CNAME(d->args[i]->type), CNAME(d->args[i]));
    }
    fputc(')', output);
}

static void cgen_decl0(Decl* d, FILE* output, int external) {
    generate_declname(d);
    switch (d->kind) {
    case Dfun:
        cgen_proto(d, output);
        fputs(";\n", output);
        break;
    case Dglobal:
        if (external || d->import) fputs("extern ", output);
        fprintf(output, "%s %s;\n", CNAME(d->v->type), CNAME(d->v));
        break;
    }

}

static void cgen_decl1(Decl* d, FILE* output) {
    int i;
    if (d->kind != Dfun || d->import) return;
    cgen_proto(d, output);
    fputs(" {\n", output);
    for (i=0; i<list_len(d->vars); ++i) {
        Var* v = d->vars[i];
        if (!v->type) continue;
        generate_varname(v);
        if (v->assign && v->uses == 1) --v->uses;
        else fprintf(output, "    %s %s;\n", CNAME(v->type), CNAME(v));
    }
    if (d->rv) {
        generate_varname(d->rv);
        if (!d->ra)
            fprintf(output, "    %s %s;\n", CNAME(d->ret), CNAME(d->rv));
    }
    for (i=0; i<list_len(d->sons); ++i) cgen_ir(d, d->sons[i], output);
    fputs("R:\n", output);
    for (i=0; i<list_len(d->vars); ++i) {
        Node* destr;
        if (d->vars[i]->type && d->vars[i]->type->kind == Tstruct &&
            !d->vars[i]->no_destr &&
            (destr = d->vars[i]->type->n->magic[Mdelete]))
            fprintf(output, "    %s(&(%s));\n", CNAME(destr->v),
                    CNAME(d->vars[i]));
    }
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
