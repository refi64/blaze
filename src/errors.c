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
    fprintf(stderr, "%s:%d:%d: %s: ", loc.file, loc.first_line, loc.first_column,
        name);
    vfprintf(stderr, fm, args);
    find_line(loc.fcont, loc.first_line, &b, &e);
    for (; *b && isspace(*b) && *b != '\n'; ++b, ++c);
    fprintf(stderr, "\n    %.*s\n", (int)e-c, b);
    fputs("    ", stderr);
    for (++c; c<loc.first_column; ++c) fputc(' ', stderr);
    for (; c<loc.last_column+1; ++c) fputc('~', stderr);
    fputc('\n', stderr);
}

#define generic_error_func(name,x) void name(Location loc, const char* fm, ...) {\
    va_list args;\
    va_start(args, fm);\
    generic_error( #name , loc, fm, args);\
    va_end(args);\
    x\
}

generic_error_func(error,++errors;)
generic_error_func(warning,++warnings;)
generic_error_func(note,)

Node* declared_here(Node* n) {
    Node* t;
    switch (n->kind) {
    case Nfun: case Ndecl: case Nid:
        if (!n->e || !n->e->n) return NULL;
        note(n->e->n->loc, "%s declared here", n->s->str);
        t = declared_here(n->e->n);
        break;
    case Nlet: case Nderef:
        t = declared_here(n->sons[0]);
        break;
    default: return NULL;
    }
    return t ? t : n;
}

void make_mutvar(Node* n, int flag, int curflags) {
    if (!n) return;
    assert(flag == Fmut || flag == Fvar);
    if (flag & Fvar && curflags & Fmut)
        note(n->loc, "change 'mut' to 'var' to make it variable");
    else {
        const char* spec = flag == Fvar ? "var" : "mut";
        const char* word = flag == Fvar ? "variable" : "mutable";
        note(n->loc, "add '%s' to make it %s", spec, word);
    }
}
