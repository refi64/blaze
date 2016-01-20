#ifndef BLAZE_H
#define BLAZE_H

#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <ds/ds.h>

#define alloc ds_zmalloc
#define ralloc ds_xrealloc
#define new(t) alloc(sizeof(t))


typedef struct String String;
typedef struct Location Location;
typedef struct Type Type;
typedef struct Node Node;
typedef struct STEntry STEntry;
typedef struct Symtab Symtab;
typedef struct Token Token;
typedef struct LexerContext LexerContext;
typedef struct Module Module;
typedef struct Decl Decl;
typedef struct Var Var;
typedef struct Instr Instr;
typedef struct GData GData;


void fatal(const char*);
int min(int a, int b);


struct String {
    char* str;
    size_t len;
};

String* string_new(const char* str);
String* string_newz(const char* str, size_t len);
String* string_clone(String* str);
void string_free(String* str);
String* string_add(String* lhs, String* rhs);
void string_merge(String* base, String* rhs);
void string_mergec(String* base, char rhs);
void string_merges(String* base, const char* rhs);


// Lists.
#define list_cat2(a,b) a##b
#define list_cat(a,b) list_cat2(a,b)
#define list_var(b) list_cat(b,__LINE__)

#define List(t) t*
#define list_lenref(l) ((size_t*)l)[-1]
#define list_len(l) (l?list_lenref(l):0)
#define list_append(l,x) do {\
    size_t list_var(len) = list_len(l);\
    l = ralloc(l?((void*)l)-sizeof(size_t):NULL,\
               sizeof(void*)*(list_var(len)+1)+sizeof(size_t))+sizeof(size_t);\
    list_lenref(l) = list_var(len)+1;\
    l[list_lenref(l)-1] = x;\
} while (0)
#define list_free(l) free(l?(void*)l-sizeof(size_t):NULL)


struct Location {
    int first_line, last_line, first_column, last_column;
    const char* file, *module, *fcont;
};
typedef Location YYLTYPE;
#define YYLTYPE_IS_DECLARED 1
#define YYLTYPE_IS_TRIVIAL 1


extern int errors, warnings;
void error(Location loc, const char* fm, ...);
void warning(Location loc, const char* fm, ...);
void note(Location loc, const char* fm, ...);

// Returns the node where it was declared.
Node* declared_here(Node* n);
// Print a message about making the node mutable or variable.
void make_mutvar(Node* n, int flag, int curflags);


// Code-generator-related data.
struct GData {
    String* cname;
    List(Decl*) sons;
    int done; // Has code for the item been generated yet? (Not always set!)
};

struct Type {
    enum {
        Tany,
        Tbuiltin,
        Tptr,
        Tstruct,
        Tfun
    } kind;
    union {
        enum {
            Tint,
            Tbyte,
            Tchar,
            Tbend
        } bkind; // Tbuiltin
        struct {
            Type** constr;
            Node* n;
        }; // Tstruct
    };
    String* name;
    // Tfun: ret, args...
    // Tstruct: members...
    // Tptr: base type
    List(Type*) sons;
    int rc; // Reference count.
    GData d;
};

enum Flags {
    Ftype=1<<0, // Type?
    Faddr=1<<1, // Addressable?
    Fmut =1<<2, // Mutable?
    Fvar =1<<3, // Variable?
    Fmv  =Fmut
         |Fvar, // Mutable or variable?
    Fcst= 1<<4, // Constant?
    Fused=1<<5, // Has the given value been used?
    Fvoid=1<<6, // Is the node void (has no type)?
    Fpure=1<<7, // Is the node/IR pure?
    Fmemb=1<<8, // Is the node/decl a member value?
    Fstc =1<<9, // Is the var a constant method/constructor of a parent struct?
};

struct Node {
    enum {
        Nid,
        Nint,

        Nsons,

        Naddr,
        Nderef,
        Ncall,
        Nattr,
        Nnew,
        Nlet,
        Nassign,
        Nreturn,
        Ntypeof,
        Nptr,
        Nstruct,
        Nconstr,
        Nfun,
        Narglist,
        Ndecl,
        Nbody,
        Nmodule
    } kind;
    union {
        struct {
            String* import, *exportc; // C import name and export name.
        }; // Nfun
        Node* constr; // Nstruct
        Node* attr; // Nattr
    };
    String* s;
    Type* type; // If Ftype is a flag, this is the referenced type.
    int typing; // Is this node being typed? (Used to locate recursion.)
    int flags;
    Location loc;
    // Naddr: expr
    // Nderef: expr
    // Ncall: target, args...
    // Nattr: base (the attribute is in s)
    // Nlet: expr
    // Nassign: target, rhs
    // Nreturn: [expr]
    // Ntypeof: expr
    // Nptr: expr
    // Nstruct: members...
    // Nconstr, Nfun: ret | NULL (always NULL for Nconstr), args...
    // Narglist: Narg...
    // Ndecl: type
    // Nbody: stmts...
    // Nmodule: tstmts...
    List(Node*) sons;
    Node* parent, *func, *this;
    STEntry* e; // Only relevant on some kinds (e.g. Nid).
    Symtab* tab; // NOTE: Only Nmodule, Nfun free their symbol tables.
    Var* v;
    Decl* d;
};
void node_dump(Node* n);
void node_free(Node* n);


// Symbol table entry.
struct STEntry {
    Node* n; // Only null with builtin types.
    Type* override; // Entry type override; only used for builtin types.
    String* name;
    /* This is the depth of the given entry in the symbol tables. If it is
       negative, then it is an attribute, and the depth is its absolute value. */
    int level;
};

struct Symtab {
    DSHtab* tab;
    // isol is used to give better errors in recursive lets.
    Symtab* parent, *isol;
    List(Symtab*) sons;
    int level;
};

extern STEntry* anytype;
extern STEntry* builtin_types[Tbend];
void init_builtin_types();
void free_builtin_types();

STEntry* stentry_new(Node* n, String* name, Type* override);
void stentry_free(STEntry* e);
Symtab* symtab_new();
STEntry* symtab_find(Symtab* tab, const char* name);
STEntry* symtab_finds(Symtab* tab, String* name);
STEntry* symtab_findl(Symtab* tab, String* name);
void symtab_add(Symtab* tab, String* name, STEntry* e);
// Create a new table whose parent is `tab`.
Symtab* symtab_sub(Symtab* tab);
void symtab_free(Symtab* tab);


void resolve(Node* n);
void type(Node* n);


void type_incref(Type* t);
void type_decref(Type* t);


struct Token {
    Location loc;
    String* s;
};

struct LexerContext {
    void* scanner;
    Node* result;
    const char* file, *module, *fcont;
};

int yyparse(LexerContext* ctx);

void lex_init();
void lex_context_init(LexerContext* ctx, const char* file, const char* module,
    const char* fcont);
void lex_context_free(LexerContext* ctx);
void lex_free();

LexerContext parse_string(const char* file, const char* module,
    const char* fcont);
LexerContext parse_file(const char* file, const char* module);


struct Module {
    List(Decl*) decls;
    List(Type*) types;
};

struct Decl {
    enum {
        Dfun,
        Dglobal,
    } kind;
    union {
        struct {
            // Instructions inside the function.
            List(Instr*) sons;
            // The variables the function declares.
            List(Var*) vars;
            // The function arguments.
            List(Var*) args;
            // Return type.
            Type* ret;
        }; // Dfun
    };
    String* name, *import, *exportc;
    // The variable associated with the decl.
    Var* v;
    // The decl's module.
    Module* m;
    int flags;
};

struct Var {
    int id; // A unique id given to each variable.
    String* name; // NULL if temporary.
    int uses; // Number of uses.
    Decl* owner;
    Instr* ir; // Instruction that created this variable (if NULL, then argument).
    Type* type;
    int flags;

    /* A "variable" may actually be a dereference or an attribute. This is to fix
       later complications with codegen. */

    int deref; // Is this a deref?
    List(Var**) av; // The attribute variables.
    Var* base; // If either of the above are truthy, this is the base var.
    int assign; // Was this generated by compiling an Nassign?
    GData d;
};

struct Instr {
    enum {
        Inull, // Optimized out.
        Inew,
        Iset,
        Iret,
        Iaddr,
        Iconstr,
        Icall,
        Iint
    } kind;
    // Destination variable.
    Var* dst;
    // Argument variables.
    List(Var*) v;
    String* s;
    int flags;
};

Var* var_new(Decl* owner, Instr* ir, Type* type, String* name);
void var_dump(Var* var);
void var_free(Var* var);

void instr_dump(Instr* ir);
void instr_free(Instr* ir);

void decl_dump(Decl* d);
void decl_free(Decl* d);

Module* igen(Node* n);
void iopt(Module* m);
void module_dump(Module* m);
void module_free(Module* m);


void cgen(Module* m, FILE* output);

#endif
