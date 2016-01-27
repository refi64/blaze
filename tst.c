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
            int i, kc = ds_hcount(modules);
            LexerContext** ctxs = (LexerContext**)ds_hvals(modules);

            for (i=0; i<kc; ++i) {
                node_dump(ctxs[i]->result);
                resolve(ctxs[i]->result);
                type(ctxs[i]->result);
            }

            if (errors == 0)
                for (i=0; i<kc; ++i) {
                    Module* m = igen(ctxs[i]->result);
                    puts("*****Unoptimized*****");
                    module_dump(m);
                    iopt(m);
                    puts("*****Optimized*****");
                    module_dump(m);
                    cgen(m, stderr);
                    module_free(m);
                }

            free(ctxs);
        }
    }
    lex_free();
    modtab_free();
    free_builtin_types();
    printf("%d\n", errors);


    return 0;
}
