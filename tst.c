#include "blaze.h"

#include <assert.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    lex_init();
    modtab_init();
    init_builtin_types();
    LexerContext* ctx = parse_file(argv[1], "__main__");
    if (ctx) {
        if (errors == 0) {
            node_dump(ctx->result);
            resolve(ctx->result);
            type(ctx->result);

            if (errors == 0) {
                Module* m = igen(ctx->result);
                puts("*****Unoptimized*****");
                module_dump(m);
                iopt(m);
                puts("*****Optimized*****");
                module_dump(m);
                cgen(m, stderr);
                module_free(m);
            }

            node_free(ctx->result);
        }
    }
    lex_free();
    modtab_free();
    free_builtin_types();
    printf("%d\n", errors);


    return 0;
}
