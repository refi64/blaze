%{
#include "blaze.h"
#include "parse.h"
#include "lex.h"

DSHtab* modules;
Node* builtins_module;

void yyerror(YYLTYPE* yylloc, LexerContext* ctx, const char* msg) {
    error(*yylloc, "%s", msg);
}

void modtab_init() {
    modules = ds_hnew((DSHashFn)strhash, (DSCmpFn)streq);
}

void modtab_free() {
    int i, kc = ds_hcount(modules);
    String** keys = (String**)ds_hkeys(modules);
    LexerContext **values = (LexerContext**)ds_hvals(modules);
    for (i=0; i<kc; ++i) {
        bassert(keys[i], "null key in module table at index %d", i);
        string_free(keys[i]);
        bassert(values[i], "null value in module table at index %d", i);
        free((void*)values[i]->fcont);
        if (values[i]->result) node_free(values[i]->result);
        lex_context_free(values[i]);
    }
    free(keys);
    free(values);
    ds_hfree(modules);
}

LexerContext* parse_file(const char* file, const char* module) {
    size_t sz;
    FILE* fp = fopen(file, "r");
    if (!fp) {
        fprintf(stderr, "error opening %s: %s\n", file, strerror(errno));
        return NULL;
    }
    char* data = readall(fp, &sz);
    if (data == NULL) {
        if (ferror(fp))
            fprintf(stderr, "error reading %s: %s\n", file, strerror(errno));
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return parse_string(file, module, data);
}

LexerContext* parse_string(const char* file, const char* module,
                           const char* fcont) {
    String* s = string_new(module);
    LexerContext* ctx = ds_hget(modules, s);
    if (ctx) {
        string_free(s);
        return ctx;
    } else {
        ctx = lex_context_init(file, module, fcont);
        yyparse(ctx);
        if (ctx->result) ctx->result->s = string_clone(s);
        ds_hput(modules, s, ctx);
        if (strcmp(module, BUILTINS) == 0) builtins_module = ctx->result;
        return ctx;
    }
}

#define scanner ctx->scanner

#define N(x,k,l) x = new(Node); x->kind = k; x->loc = l;

#define B(x,o,l,t,r) {\
    N(x, Nop, t.loc)\
    list_append(x->sons, l);\
    list_append(x->sons, r);\
    x->op = o;\
}
%}

%union {
    Token t;
    Node* n;
    List(Node*) l;
    struct {
        Node* rn;
        String* rs;
        int import, exportc;
    } funbody;
    int i;
}

%define parse.error verbose
%define parse.lac full
%define api.pure full
%locations

%parse-param { LexerContext* ctx }
%lex-param { void* scanner  }

%token <t> TINDENT
%token <t> TUNINDENT
%token <t> TNEWL
%token <t> TSEP
%token <t> TERROR
%token <t> TINDERROR

%token <t> TARROW
%token <t> TDOT
%token <t> TLP
%token <t> TRP
%token <t> TLBK
%token <t> TRBK
%token <t> TCOMMA
%token <t> TCOLON
%token <t> TDCOLON
%token <t> TSEMIC
%token <t> TEQ

%token <t> TDEQ
%token <t> TNE
%token <t> TLT
%token <t> TGT

%token <t> TPLUS
%token <t> TMINUS
%token <t> TSLASH

%token <t> TSTAR
%token <t> TAND

%token <t> TAT

%token <t> TFUN
%token <t> TLET
%token <t> TMUT
%token <t> TVAR
%token <t> TRETURN
%token <t> TTYPEOF
%token <t> TEXPORTC
%token <t> TGLOBAL
%token <t> TSTRUCT
%token <t> TNEW
%token <t> TDELETE
%token <t> TEXPORT
%token <t> TIF
%token <t> TID
%token <t> TINT
%token <t> TSTRING

%precedence TNEWL TSEMIC TSEP
%precedence PPROG

%left TDCOLON
%nonassoc TDEQ TNE TLT TGT
%left TPLUS TMINUS
%left TSTAR TSLASH
%right UTAND UTSTAR
%left TNEW
%left TDOT TLP TLBK

%type <n> tstmt
%type <n> tstmt2
%type <n> struct
%type <l> members
%type <l> members2
%type <n> member
%type <n> constr
%type <n> destr
%type <n> fun
%type <n> funret
%type <funbody> funbody
%type <n> globalsuf
%type <n> global
%type <n> arglist
%type <n> arglist2
%type <n> decl
%type <n> body
%type <n> body2
%type <n> stmt
%type <n> let
%type <n> assign
%type <n> return
%type <n> if
%type <n> texpr
%type <i> mutptr
%type <n> typeof
%type <n> expr
%type <n> name
%type <n> this
%type <n> id
%type <n> dec
%type <n> ptr
%type <n> call
%type <l> callargs
%type <l> callargs2
%type <n> index
%type <n> lattr
%type <n> attr
%type <n> new
%type <n> cast
%type <n> op

%type <i> modspec

%destructor { if ($$.s) string_free($$.s); } <t>
%destructor { node_free($$); } <n>

%%

prog : prog2
     | ws { N(ctx->result, Nmodule, yylloc) }

prog2 : osep tstmt {
    N(ctx->result, Nmodule, yylloc)
    list_append(ctx->result->sons, $2);
}   | prog2 sep tstmt %prec PPROG { list_append(ctx->result->sons, $3); }
    | prog2 sep %prec PPROG {}

tstmt2 : struct | fun | global

tstmt : tstmt2         { $$ = $1; }
      | TEXPORT tstmt2 { $$ = $2; $$->export = 1; }

struct : TSTRUCT id TCOLON members {
    int i;
    N($$, Nstruct, $2->loc)
    $$->s = string_clone($2->s);
    node_free($2);
    for (i=0; i<list_len($4); ++i) list_append($$->sons, $4[i]);
    list_free($4);
}

members : indent members2 unindent { $$ = $2; }

members2 : member { $$ = NULL; list_append($$, $1); }
         | members2 sep member { $$ = $1; list_append($$, $3); }

member : constr | destr | fun
       | modspec decl { $$ = $2; $$->flags |= $1; }

constr : TNEW arglist TCOLON body {
    N($$, Nconstr, $1.loc)
    list_append($$->sons, NULL);
    list_append($$->sons, $2);
    list_append($$->sons, $4);
}

destr : TDELETE TCOLON body {
    N($$, Ndestr, $1.loc)
    list_append($$->sons, NULL);
    list_append($$->sons, NULL);
    list_append($$->sons, $3);
}

fun : TFUN id arglist funret funbody {
    N($$, Nfun, $2->loc)
    $$->s = string_clone($2->s);
    node_free($2);
    list_append($$->sons, $4);
    list_append($$->sons, $3);
    if ($5.import) $$->import = $5.rs;
    else list_append($$->sons, $5.rn);
    if ($5.exportc) $$->exportc = $5.rs;
    $$->flags |= Fcst | Faddr;
}

funbody : TCOLON body { $$.exportc = 0; $$.import = 0; $$.rn = $2; }
        | TEXPORTC TSTRING TCOLON body {
            $$.exportc = 1;
            $$.import = 0;
            $$.rs = $2.s;
            $$.rn = $4;
        }
        | TSTRING     { $$.exportc = 0; $$.import = 1; $$.rs = $1.s; }

funret : { $$ = NULL; }
       | TARROW texpr { $$ = $2; }

arglist :         { N($$, Narglist, yylloc) }
        | TLP TRP { N($$, Narglist, $1.loc); }
        | TLP arglist2 TRP { $$ = $2; $$->loc = $1.loc; }

arglist2 : decl { N($$, Narglist, $1->loc); list_append($$->sons, $1); }
         | arglist2 TCOMMA decl { $$ = $1; list_append($$->sons, $3); }

globalsuf :          { $$ = NULL; }
          | TEQ expr { $$ = $2; }

global : TGLOBAL modspec decl globalsuf {
    $$ = $3;
    $$->flags |= $2;
    $$->sons[1] = $4;
}      | TGLOBAL modspec decl TSTRING {
    $$ = $3;
    $$->flags |= $2;
    $$->import = $4.s;
}

decl : id TCOLON texpr {
    N($$, Ndecl, $1->loc);
    $$->s = string_clone($1->s);
    node_free($1);
    list_append($$->sons, $3);
    list_append($$->sons, NULL);
}

body : indent body2 unindent { $$ = $2; }
     | stmt { N($$, Nbody, $1->loc); list_append($$->sons, $1); }

body2 : stmt { N($$, Nbody, $1->loc); list_append($$->sons, $1); }
      | body2 sep stmt { $$ = $1; list_append($$->sons, $3); }

osep : | sep
sep : sepone | sep sepone
sepone : TSEMIC | TNEWL | TSEP

stmt : let    { $$ = $1; }
     | assign { $$ = $1; }
     | return { $$ = $1; }
     | call   { $$ = $1; }
     | if     { $$ = $1; }

let : TLET modspec id TEQ expr {
    N($$, Nlet, $3->loc)
    $$->s = string_clone($3->s);
    node_free($3);
    list_append($$->sons, $5);
    $$->flags |= $2;
}

modspec :      { $$ = 0; }
        | TMUT { $$ = Fmut; }
        | TVAR { $$ = Fvar; }

assign : expr TEQ expr {
    N($$, Nassign, $2.loc);
    list_append($$->sons, $1);
    list_append($$->sons, $3);
}

return : TRETURN expr {
    N($$, Nreturn, $1.loc)
    list_append($$->sons, $2);
}      | TRETURN { N($$, Nreturn, $1.loc) }

if : TIF expr TCOLON body {
    N($$, Nif, $1.loc)
    list_append($$->sons, $2);
    list_append($$->sons, $4);
}

texpr : name   { $$ = $1; }
      | typeof { $$ = $1; }
      | TSTAR mutptr texpr {
          N($$, Nptr, $1.loc);
          list_append($$->sons, $3);
          $$->flags |= $2;
      }

mutptr :      { $$ = 0; }
       | TMUT { $$ = Fmut; }

typeof : TTYPEOF TLP expr TRP {
    N($$, Ntypeof, $1.loc)
    list_append($$->sons, $3);
}

expr : name { $$ = $1; }
     | dec  { $$ = $1; }
     | ptr  { $$ = $1; }
     | call { $$ = $1; }
     | index { $$ = $1; }
     | attr { $$ = $1; }
     | new  { $$ = $1; }
     | cast { $$ = $1; }
     | op { $$ = $1; }

     | TLP expr TRP { $$ = $2; }

name : id   { $$ = $1; $$->flags |= Faddr; }
     | this { $$ = $1; $$->flags |= Faddr; }

id : TID {
    N($$, Nid, $1.loc)
    $$->s = $1.s;
}

this : TAT {
    N($$, Nid, $1.loc)
    $$->s = string_new("@");
}

dec : TINT {
    N($$, Nint, $1.loc)
    $$->s = $1.s;
    $$->flags |= Fcst;
}

ptr : TSTAR expr {
    N($$, Nderef, $1.loc)
    list_append($$->sons, $2);
    $$->flags |= Faddr;
} %prec UTAND
    | TAND expr {
    N($$, Naddr, $1.loc)
    list_append($$->sons, $2);
} %prec UTSTAR

call : expr TLP callargs TRP {
    int i;
    N($$, Ncall, $1->loc)
    list_append($$->sons, $1);
    for (i=0; i<list_len($3); ++i) list_append($$->sons, $3[i]);
    list_free($3);
}

callargs :           { $$ = NULL; }
         | callargs2 { $$ = $1; }

callargs2 : expr { $$ = NULL; list_append($$, $1); }
          | callargs2 TCOMMA expr { $$ = $1; list_append($$, $3); }

index : expr TLBK expr TRBK {
    N($$, Nindex, $2.loc)
    list_append($$->sons, $1);
    list_append($$->sons, $3);
    $$->flags |= Faddr;
}

lattr : expr TDOT { $$ = $1; }
      | this      { $$ = $1; }

attr : lattr TID {
    N($$, Nattr, $2.loc)
    $$->s = $2.s;
    list_append($$->sons, $1);
    $$->flags |= Faddr;
}

new : TNEW texpr TLP callargs TRP {
    int i;
    N($$, Nnew, $2->loc)
    list_append($$->sons, $2);
    for (i=0; i<list_len($4); ++i) list_append($$->sons, $4[i]);
    list_free($4);
}   | TNEW texpr {
    N($$, Nnew, $2->loc)
    list_append($$->sons, $2);
}

cast : expr TDCOLON texpr {
    N($$, Ncast, $2.loc)
    list_append($$->sons, $1);
    list_append($$->sons, $3);
}

op : expr TPLUS expr { B($$, Oadd, $1, $2, $3) }
   | expr TMINUS expr { B($$, Osub, $1, $2, $3) }
   | expr TSTAR expr { B($$, Omul, $1, $2, $3) }
   | expr TSLASH expr { B($$, Odiv, $1, $2, $3) }
   | expr TDEQ expr { B($$, Oeq, $1, $2, $3) }
   | expr TNE expr { B($$, One, $1, $2, $3) }
   | expr TLT expr { B($$, Olt, $1, $2, $3) }
   | expr TGT expr { B($$, Ogt, $1, $2, $3) }

ws : | ws TNEWL
/* eof : TEOF */
indent : TNEWL TINDENT
unindent : sep TUNINDENT
