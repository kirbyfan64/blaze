#include "blaze.h"

const char* typenames[] = {"int", "char"};
int type_id=0;

#define CNAME(x) (x?x->d.cname->str:"void")

static void generate_basename(char p, GData* d, String* name, int id) {
    char buf[1024];
    if (d->cname) return;
    d->cname = string_newz(&p, 1);
    snprintf(buf, sizeof(buf), "%d", id);
    string_merges(d->cname, buf);
    if (name) {
        string_mergec(d->cname, '_');
        string_merge(d->cname, name);
    }
}

static void generate_typename(Type* t) {
    generate_basename('t', &t->d, t->name, type_id++);
}

static void generate_argname(Var* v) {
    generate_basename('a', &v->d, v->name, v->id);
}

static void generate_varname(Var* v) {
    generate_basename('v', &v->d, v->name, v->id);
}

static void generate_declname(Decl* d) {
    if (d->v->name && strcmp(d->v->name->str, "main") == 0)
        d->v->d.cname = string_new("main");
    else generate_basename('f', &d->v->d, d->v->name, d->v->id);
}

static void cgen_typedef(Type* t, FILE* output) {
    int i;
    assert(t);
    if (t->d.cname) return;
    for (i=0; i<list_len(t->sons); ++i)
        if (t->sons[i]) cgen_typedef(t->sons[i], output);
    switch (t->kind) {
    case Tany: assert(0);
    case Tbuiltin:
        t->d.cname = string_new(typenames[t->bkind]);
        break;
    case Tptr:
        generate_typename(t);
        fprintf(output, "typedef %s* %s;\n", CNAME(t->sons[0]), CNAME(t));
        break;
    case Tfun:
        generate_typename(t);
        fprintf(output, "typedef %s", CNAME(t->sons[0]));
        fprintf(output, " (*%s)(", CNAME(t));
        for (i=1; i<list_len(t->sons); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(t->sons[i]), output);
        }
        fputs(");\n", output);
        break;
    }
}

static void cgen_ir(Instr* ir, FILE* output) {
    int i;
    if (ir->kind == Inull) return;
    fputs("    ", output);
    if (ir->dst && ir->dst->type) fprintf(output, "%s = ", CNAME(ir->dst));
    switch (ir->kind) {
    case Inull: assert(0);
    case Iret:
        fputs("return", output);
        if (ir->v) fprintf(output, " %s", CNAME(ir->v[0]));
        break;
    case Iset:
        if (ir->v[0]->ir->kind == Iaddr) fputs(CNAME(ir->v[0]->ir->v[0]), output);
        else fprintf(output, "*%s", CNAME(ir->v[0]));
        fprintf(output, " = %s", CNAME(ir->v[1]));
        break;
    case Inew:
        fputs(CNAME(ir->v[0]), output);
        break;
    case Icall:
        fprintf(output, "%s(", CNAME(ir->v[0]));
        for (i=1; i<list_len(ir->v); ++i) {
            if (i > 1) fputs(", ", output);
            fputs(CNAME(ir->v[i]), output);
        }
        fputc(')', output);
        break;
    case Ideref:
        fprintf(output, "*%s", CNAME(ir->v[0]));
        break;
    case Iaddr:
        fprintf(output, "&%s", CNAME(ir->v[0]));
        break;
    case Iint:
        fputs(ir->s->str, output);
        break;
    }
    fputs(";\n", output);
}

static void cgen_proto(Decl* d, FILE* output) {
    int i;
    if (d->import) d->v->d.cname = string_clone(d->import);
    else generate_declname(d);
    fprintf(output, "%s %s(", CNAME(d->v->type->sons[0]), CNAME(d->v));
    for (i=0; i<list_len(d->args); ++i) {
        generate_argname(d->args[i]);
        if (i) fputs(", ", output);
        fprintf(output, "%s %s", CNAME(d->args[i]->type), CNAME(d->args[i]));
    }
    fputc(')', output);
}

static void cgen_decl0(Decl* d, FILE* output) {
    cgen_proto(d, output);
    fputs(";\n", output);
}

static void cgen_decl1(Decl* d, FILE* output) {
    int i;
    if (d->import) return;
    cgen_proto(d, output);
    fputs(" {\n", output);
    for (i=0; i<list_len(d->vars); ++i) {
        if (!d->vars[i]->type) continue;
        generate_varname(d->vars[i]);
        fprintf(output, "    %s %s;\n", CNAME(d->vars[i]->type),
                CNAME(d->vars[i]));
    }
    for (i=0; i<list_len(d->sons); ++i) cgen_ir(d->sons[i], output);
    fputs("}\n\n", output);
}

#define FREE_CNAME(b) do {\
        String** p = &(b)->d.cname;\
        if (*p) string_free(*p);\
        *p = NULL;\
    } while (0)

static void free_type_cnames(Type* t) {
    int i;
    if (!t) return;
    FREE_CNAME(t);
    for (i=0; i<list_len(t->sons); ++i) free_type_cnames(t->sons[i]);
}

void cgen(Module* m, FILE* output) {
    int i;
    for (i=0; i<list_len(m->types); ++i)
        cgen_typedef(m->types[i], output);
    fputs("\n\n", output);

    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl0(m->decls[i], output);
    fputs("\n\n", output);

    for (i=0; i<list_len(m->decls); ++i)
        cgen_decl1(m->decls[i], output);

    for (i=0; i<list_len(m->types); ++i) free_type_cnames(m->types[i]);
    for (i=0; i<list_len(m->decls); ++i) {
        int j;
        Decl* d = m->decls[i];
        string_free(d->v->d.cname);
        for (j=0; j<list_len(d->args); ++j) FREE_CNAME(d->args[j]);
        for (j=0; j<list_len(d->vars); ++j) FREE_CNAME(d->vars[j]);
    }
}

#undef FREE_CNAME
