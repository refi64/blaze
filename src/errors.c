#include "blaze.h"
#include <stdarg.h>
#include <ctype.h>

int errors=0, warnings=0;

static void find_line(const char* string, int line, const char** b, size_t* e) {
    const char* p;
    while (line-1) {
        p = strchr(string, '\n');
        if (!p) {
            *b = string;
            *e = strlen(string);
            return;
        }
        --line;
        string = p+1;
    }
    *b = string;
    p = strchr(string, '\n');
    *e = p ? p-string : strlen(string);
}

static inline void generic_error(const char* name, Location loc, const char* fm,
    va_list args) {
    const char* b;
    size_t e;
    int c=0;
    fprintf(stderr, "\033[1m%s:%d:%d: %s: \033[0m\033[1m", loc.file,
        loc.first_line, loc.first_column, name);
    vfprintf(stderr, fm, args);
    fputs("\033[0m", stderr);
    find_line(loc.fcont, loc.first_line, &b, &e);
    for (; *b && isspace(*b) && *b != '\n'; ++b, ++c);
    fprintf(stderr, "\n    %.*s\n", (int)e-c, b);
    fputs("    ", stderr);
    for (++c; c<loc.first_column; ++c) fputc(' ', stderr);
    fputs("\033[32m", stderr);
    for (; c<loc.last_column+1; ++c) fputc('~', stderr);
    fputs("\033[0m\n", stderr);
}

#define generic_error_func(n,c,x) void n(Location loc, const char* fm, ...) {\
    va_list args;\
    va_start(args, fm);\
    generic_error("\033[3" #c "m" #n , loc, fm, args);\
    va_end(args);\
    x\
}

generic_error_func(error,1,++errors;)
generic_error_func(warning,5,++warnings;)
generic_error_func(note,6,)

Node* declared_here(Node* n) {
    Node* t = NULL;

    switch (n->kind) {
    case Nfun: case Ndecl:
        note(n->loc, "'%s' declared here", n->s->str);
        break;
    case Nid:
        if (n->e && n->e->n && n->e->n->loc.file) {
            if (n->e->n->kind != Nfun && n->e->n->kind != Ndecl)
                note(n->e->n->loc, "'%s' declared here", n->s->str);
            t = declared_here(n->e->n);
        }
        break;
    case Nlet: case Naddr: case Nderef: case Nindex:
        t = declared_here(n->sons[0]);
        break;
    case Nattr:
        declared_here(n->sons[0]);
        if (n->attr) t = declared_here(n->attr);
        break;
    default: return NULL;
    }
    return t ? t : n;
}

void make_mutvar(Node* n, int flag, int curflags) {
    if (!n) return;
    bassert(flag == Fmut || flag == Fvar, "unexpected flag %d", flag);
    if (flag & Fvar && curflags & Fmut)
        note(n->loc, "change 'mut' to 'var' to make it variable");
    else {
        const char* spec = flag == Fvar ? "var" : "mut";
        const char* word = flag == Fvar ? "variable" : "mutable";
        note(n->loc, "add '%s' to make it %s", spec, word);
    }
}
