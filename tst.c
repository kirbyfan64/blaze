#include "blaze.h"

#include <assert.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    lex_init();
    modtab_init();
    init_builtin_types();
    assert(parse_file(LIBDIR "builtins.blz", "builtins"));
    LexerContext* ctx = parse_file(argv[1], "__main__");
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

            if (errors == 0)
                for (i=0; i<kc; ++i) {
                    Node* n = ctxs[i]->result;
                    Module* m = igen(n);
                    printf("##########Module %s:\n", n->s->str);
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
