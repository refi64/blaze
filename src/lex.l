%{
#include "blaze.h"
#include "parse.h"

#define YY_EXTRA_TYPE LexerContext*

int cur=0, level=0, sp=0, sep=0;

#define YY_USER_ACTION yylloc->first_line = yylloc->last_line = yylineno;\
    yylloc->first_column = yycolumn;\
    yylloc->last_column = yycolumn+yyleng-1;\
    yylloc->file = yyextra->file;\
    yylloc->module = yyextra->module;\
    yylloc->fcont = yyextra->fcont;\
    yycolumn += yyleng;\
    yylval->t.loc = *yylloc;\
    yylval->t.s = NULL;

int yylex_init(yyscan_t*);
int yylex_init_extra(YY_EXTRA_TYPE, yyscan_t*);
int yylex_destroy(yyscan_t);
void yyset_extra(YY_EXTRA_TYPE, yyscan_t);
void yyset_debug(int, yyscan_t);
void yyset_lineno(int, yyscan_t);
void yyset_column(int, yyscan_t);

static DSHtab* kw = NULL;

void lex_init() {
    kw = ds_hnew(ds_strhash, ds_streq);
    #define KW(s,t) ds_hput(kw, #s, (void*)(intptr_t)t);
    KW(fun, TFUN)
    KW(let, TLET)
    KW(mut, TMUT)
    KW(var, TVAR)
    KW(return, TRETURN)
    KW(typeof, TTYPEOF)
    KW(exportc, TEXPORTC)
    KW(global, TGLOBAL)
    KW(struct, TSTRUCT)
    KW(new, TNEW)
    KW(delete, TDELETE)
    KW(export, TEXPORT)
    KW(if, TIF)
}

LexerContext* lex_context_init(const char* file, const char* module,
                               const char* fcont) {
    LexerContext* ctx = new(LexerContext);
    ctx->file = file;
    ctx->module = module;
    ctx->fcont = fcont;
    ctx->result = NULL;
    yylex_init(&ctx->scanner);
    yyset_debug(1, ctx->scanner);
    yyset_extra(ctx, ctx->scanner);
    yy_scan_string(fcont, ctx->scanner);
    yyset_lineno(1, ctx->scanner);
    yyset_column(1, ctx->scanner);
    return ctx;
}

void lex_context_free(LexerContext* ctx) {
    if (ctx->scanner) yylex_destroy(ctx->scanner);
    free(ctx);
}

void lex_free() { ds_hfree(kw); }

#define ERROR

%}

%x mcomment indent

%option noyywrap yylineno reentrant bison-bridge bison-locations

%%

<<EOF>> {
    cur = 0;
    if (level) {
        level--;
        return TUNINDENT;
    } else return 0;
}
"#[" { BEGIN(mcomment); }
[ \t]+ {}
#.* {}
"\\\n" { yycolumn = 1; }
"\n"+ { yycolumn = 1; BEGIN indent; return TNEWL; }
"->" { return TARROW; }
"." { return TDOT; }
"(" { return TLP; }
")" { return TRP; }
"[" { return TLBK; }
"]" { return TRBK; }
"," { return TCOMMA; }
"::" { return TDCOLON; }
":" { return TCOLON; }
";" { return TSEMIC; }
"=" { return TEQ; }

"==" { return TDEQ; }
"!=" { return TNE; }
"<" { return TLT; }
">" { return TGT; }

"+" { return TPLUS; }
"-" { return TMINUS; }
"/" { return TSLASH; }

"*" { return TSTAR; }
"&" { return TAND; }

"@" { return TAT; }

[a-zA-Z_][a-zA-Z0-9_]* {
    intptr_t p = (intptr_t)ds_hget(kw, yytext);
    if (p) return (int)p;
    else {
        yylval->t.s = string_new(yytext);
        return TID;
    }
}
\"([^\\\"]|\\.)*\" {
    // Remove quotes.
    yylval->t.s = string_newz(yytext+1, yyleng-2);
    return TSTRING;
}
[0-9]+ { yylval->t.s = string_new(yytext); return TINT; }
. { return TERROR; }

<mcomment>"]#" { BEGIN(INITIAL); }
<mcomment>"\n"+ { yycolumn = 1; }
<mcomment>. {}

<indent>[ \t]+# { unput('#'); BEGIN(INITIAL); }
<indent>" " { ++cur; }
<indent>"\t" { ERROR; }
<indent>.  {
    unput(*yytext);
    --yycolumn;
    if (sp == 0) sp = cur;
    if (sp != 0 && cur % sp) {
        /* char buf[1024]; */
        /* sprintf(buf, "indentation count must be a mul %d", ); */
        /* yylval.s = string_new("indentation must be unified"); */
        return TINDERROR;
    }
    if (sp != 0 && cur/sp != level) {
        int curlvl = cur/sp;
        if (curlvl > level) {
            ++level;
            cur = level*sp;
            return TINDENT;
        } else if (curlvl < level) {
            --level;
            if (curlvl < level) {
                // Put the chars back in the stream so it can be re-unindented.
                int i;
                for (i=0; i<sp; ++i) unput(' ');
            }
            sep = 1;
            return TUNINDENT;
        }
    } else if (sep) {
        sep = 0;
        return TSEP;
    } else {
        cur = 0;
        BEGIN INITIAL;
    }
}
