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
%token <t> TLP
%token <t> TRP
%token <t> TCOMMA
%token <t> TCOLON
%token <t> TSEMIC
%token <t> TEQ

%token <t> TSTAR
%token <t> TAND

%token <t> TFUN
%token <t> TLET
%token <t> TMUT
%token <t> TVAR
%token <t> TRETURN
%token <t> TTYPEOF
%token <t> TID
%token <t> TINT

%type <n> tstmt
%type <n> fun
%type <n> funret
%type <n> arglist
%type <n> arglist2
%type <n> arg
%type <n> body
%type <n> body2
%type <n> stmt
%type <n> let
%type <n> assign
%type <n> return
%type <n> texpr
%type <n> typeof
%type <n> expr
%type <n> name
%type <n> id
%type <n> dec
%type <n> ptr
%type <n> call
%type <l> callargs
%type <l> callargs2

%type <i> letspec

%destructor { if ($$.s) string_free($$.s); } <t>
%destructor { node_free($$); } <n>

%%

prog : ws { N(ctx->result, Nmodule, yylloc) }
     | prog tstmt ws { list_append(ctx->result->sons, $2); }

tstmt : fun

fun : TFUN id arglist funret TCOLON body {
    N($$, Nfun, $2->loc)
    $$->s = string_clone($2->s);
    node_free($2);
    list_append($$->sons, $4);
    list_append($$->sons, $3);
    list_append($$->sons, $6);
    $$->flags |= Fcst | Faddr;
}

funret : { $$ = NULL; }
       | TARROW texpr { $$ = $2; }

arglist :         { N($$, Narglist, yylloc) }
        | TLP TRP { N($$, Narglist, $1.loc); }
        | TLP arglist2 TRP { $$ = $2; $$->loc = $1.loc; }

arglist2 : arg { N($$, Narglist, $1->loc); list_append($$->sons, $1); }
         | arglist2 TCOMMA arg { $$ = $1; list_append($$->sons, $3); }

arg : id TCOLON texpr {
    N($$, Narg, $1->loc);
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

let : TLET letspec id TEQ expr {
    N($$, Nlet, $3->loc)
    $$->s = string_clone($3->s);
    node_free($3);
    list_append($$->sons, $5);
    $$->flags |= $2;
}

letspec :      { $$ = 0; }
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
      | TSTAR texpr { N($$, Nptr, $1.loc); list_append($$->sons, $2); }

typeof : TTYPEOF TLP expr TRP {
    N($$, Ntypeof, $1.loc)
    list_append($$->sons, $3);
}

expr : name { $$ = $1; }
     | dec  { $$ = $1; }
     | ptr  { $$ = $1; }
     | call { $$ = $1; }

name : id { $$ = $1; $$->flags |= Faddr; }

id : TID {
    N($$, Nid, $1.loc)
    $$->s = $1.s;
}

dec : TINT {
    N($$, Nint, $1.loc)
    $$->s = $1.s;
    $$->flags |= Fcst;
}

ptr : TSTAR expr {
    N($$, Nderef, $1.loc)
    list_append($$->sons, $2);
}   | TAND expr {
    N($$, Naddr, $1.loc)
    list_append($$->sons, $2);
}

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

ws : | ws TNEWL
/* eof : TEOF */
indent : TNEWL TINDENT
unindent : TNEWL TUNINDENT
