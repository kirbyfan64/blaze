#include "blaze.h"

static void igen_sons(Func* f, Node* n);

static Var* igen_node(Func* f, Node* n) {
    Instr* ir = new(Instr);
    switch (n->kind) {
    case Nbody:
        igen_sons(f, n);
        instr_free(ir);
        ir = NULL;
        break;
    case Nreturn:
        ir->kind = Iret;
        if (n->sons[0]) ir->v = igen_node(f, n->sons[0]);
        break;
    case Nlet:
        ir->kind = n->kind == Nlet ? Inew : Iset;
        ir->dst = var_new(f, n->s);
        n->v = ir->v = igen_node(f, n->sons[0]);
        assert(ir->v);
        break;
    case Nassign:
        /* assert(n->sons[0]); */
        break;
    case Nid:
        ir->kind = Ivar;
        assert(n->e && n->e->n && n->e->n->v);
        ir->dst = ir->v = n->e->n->v;
        break;
    case Nint:
        ir->kind = Iint;
        ir->s = string_clone(n->s);
        ir->dst = var_new(f, NULL);
        break;
    case Nmodule: case Ntypeof: case Nfun: case Narglist: case Narg:
    case Nsons: assert(0);
    }
    if (ir) {
        list_append(f->sons, ir);
        if (ir->dst) list_append(f->vars, ir->dst);
        if (ir->v) ++ir->v->uses;
        return ir->dst;
    } else return NULL;
}

static void igen_sons(Func* f, Node* n) {
    int i;
    assert(n && n->kind > Nsons);
    for (i=0; i<list_len(n->sons); ++i)
        igen_node(f, n->sons[i]);
}

static Func* igen_func(Node* n) {
    assert(n->kind == Nfun);
    Func* res = new(Func);
    res->name = string_clone(n->s);
    igen_node(res, n->sons[2]);
    return res;
}

List(Func*) igen(Node* n) {
    List(Func*) res = NULL;
    int i, j, k;
    assert(n);

    for (i=0; i<list_len(n->sons); ++i) switch (n->sons[i]->kind) {
    case Nmodule:
        for (j=0; j<list_len(n->sons[i]->sons); ++j) {
            List(Func*) t = igen(n->sons[i]->sons[j]);
            for (k=0; k<list_len(t); ++k) list_append(res, t[k]);
        }
        break;
    case Nfun:
        list_append(res, igen_func(n->sons[i]));
        break;
    default: break;
    }

    return res;
}
