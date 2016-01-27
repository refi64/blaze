#include "blaze.h"

#include <assert.h>

int main(int argc, char** argv) {
    LexerContext* ctx;
    assert(argc == 2);
    lex_init();
    modtab_init();
    init_builtin_types();
    assert(parse_file(LIBDIR BUILTINS ".blz", BUILTINS));

    ctx = parse_file(argv[1], "__main__");
    if (ctx) {
        if (errors == 0) {
            int i, kc = ds_hcount(modules);
            LexerContext** ctxs = (LexerContext**)ds_hvals(modules);

            for (i=0; i<kc; ++i) {
                Node* n = ctxs[i]->result;
                printf("##########Module %s:\n", n->s->str);
                /* node_dump(n); */
                resolve(n);
                type(n);
            }

            if (errors == 0) {
                List(Module*) mods = NULL;
                for (i=0; i<kc; ++i) {
                    Node* n = ctxs[i]->result;
                    Module* m = igen(n);
                    printf("##########Module %s:\n", n->s->str);
                    puts("*****Unoptimized*****");
                    module_dump(m);
                    iopt(m);
                    puts("*****Optimized*****");
                    module_dump(m);
                    list_append(mods, m);
                }

                for (i=0; i<list_len(mods); ++i) {
                    printf("##########Module %s:\n", ctxs[i]->result->s->str);
                    cgen(mods[i], stderr);
                }
                for (i=0; i<list_len(mods); ++i) {
                    cgen_free(mods[i]);
                    module_free(mods[i]);
                }
                list_free(mods);
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
