%{
#include "blaze.h"
#include "parse.h"
#include "lex.h"

void yyerror(YYLTYPE* yylloc, LexerContext* ctx, const char* msg) {
    error(*yylloc, "%s", msg);
}

LexerContext parse_string(const char* file, const char* module,
    const char* fcont) {
    LexerContext ctx;
    lex_context_init(&ctx, file, module, fcont);
    yyparse(&ctx);
    return ctx;
}

#define scanner ctx->scanner

#define N(x,k,l) x = new(Node); x->kind = k; x->loc = l;
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
%token <t> TID
%token <t> TINT
%token <t> TSTRING

%right UTAND UTSTAR
%left TDCOLON
%left TNEW
%left TDOT TLP TLBK

%type <n> tstmt
%type <n> struct
%type <l> members
%type <l> members2
%type <n> member
%type <n> constr
%type <n> fun
%type <n> funret
%type <funbody> funbody
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

%type <i> modspec

%destructor { if ($$.s) string_free($$.s); } <t>
%destructor { node_free($$); } <n>

%%

prog : ws { N(ctx->result, Nmodule, yylloc) }
     | prog tstmt ws { list_append(ctx->result->sons, $2); }

tstmt : struct | fun | global

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

member : constr | fun
       | modspec decl { $$ = $2; $$->flags |= $1; }

constr : TNEW arglist TCOLON body {
    N($$, Nconstr, $1.loc);
    list_append($$->sons, NULL);
    list_append($$->sons, $2);
    list_append($$->sons, $4);
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

global : TGLOBAL modspec decl { $$ = $3; $$->flags |= $2; }

decl : id TCOLON texpr {
    N($$, Ndecl, $1->loc);
    $$->s = string_clone($1->s);
    node_free($1);
    list_append($$->sons, $3);
}

body : indent body2 unindent { $$ = $2; }
     | stmt { N($$, Nbody, $1->loc); list_append($$->sons, $1); }

body2 : stmt { N($$, Nbody, $1->loc); list_append($$->sons, $1); }
      | body2 sep stmt { $$ = $1; list_append($$->sons, $3); }

sep : TSEMIC | TNEWL

stmt : let    { $$ = $1; }
     | assign { $$ = $1; }
     | return { $$ = $1; }
     | call   { $$ = $1; }

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

ws : | ws TNEWL
/* eof : TEOF */
indent : TNEWL TINDENT
unindent : TNEWL TUNINDENT
