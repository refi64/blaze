#include "blaze.h"

int main(int argc, char** argv) {
    char buf[2048];
    FILE* f;
    assert(argc == 2);
    assert((f = fopen(argv[1], "r")));
    buf[fread(buf, 1, sizeof(buf), f)] = 0;
    fclose(f);
    lex_init();
    init_builtin_types();
    LexerContext ctx;
    lex_context_init(&ctx, argv[1], "__main__", buf);
    yyparse(&ctx);
    if (ctx.result) {
        node_dump(ctx.result);
        resolve(ctx.result);
        type(ctx.result);

        if (errors == 0) {
            List(Func*) ir = igen(ctx.result);
            int i;
            for (i=0; i<list_len(ir); ++i) func_dump(ir[i]);
            for (i=0; i<list_len(ir); ++i) func_free(ir[i]);
            list_free(ir);
        }

        node_free(ctx.result);
    }
    lex_context_free(&ctx);
    lex_free();
    free_builtin_types();
    printf("%d\n", errors);


    return 0;
}
