#include "blaze.h"

int main(int argc, char** argv) {
    char buf[4096];
    FILE* f;
    assert(argc == 2);
    assert((f = fopen(argv[1], "r")));
    buf[fread(buf, 1, sizeof(buf), f)] = 0;
    fclose(f);
    lex_init();
    init_builtin_types();
    LexerContext ctx = parse_string(argv[1], "__main__", buf);
    if (ctx.result) {
        node_dump(ctx.result);
        resolve(ctx.result);
        type(ctx.result);

        if (errors == 0) {
            Module* m = igen(ctx.result);
            puts("*****Unoptimized*****");
            module_dump(m);
            iopt(m);
            puts("*****Optimized*****");
            module_dump(m);
            cgen(m, stderr);
            module_free(m);
        }

        node_free(ctx.result);
    }
    lex_context_free(&ctx);
    lex_free();
    free_builtin_types();
    printf("%d\n", errors);


    return 0;
}
