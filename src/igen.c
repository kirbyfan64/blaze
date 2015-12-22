#include "blaze.h"

static int decl_counter = 0;

static void igen_sons(Decl* f, Node* n);
static Var* igen_node(Decl* f, Node* n);

static Var* igen_address(Decl* f, Node* n) {
    Instr* ir = new(Instr);
    assert(n->flags & Faddr);
    ir->kind = Iaddr;
    list_append(ir->v, igen_node(f, n));
    ir->dst = var_new(f, ir, n->type, NULL);
    assert(ir->dst);
    list_append(f->sons, ir);
    return ir->dst;
}

static Var* igen_node(Decl* f, Node* n) {
    Instr* ir = new(Instr);
    switch (n->kind) {
    case Nbody:
        igen_sons(f, n);
        instr_free(ir);
        ir = NULL;
        break;
    case Nreturn:
        ir->kind = Iret;
        if (n->sons) list_append(ir->v, igen_node(f, n->sons[0]));
        break;
    case Nlet:
        ir->kind = Inew;
        n->v = ir->dst = var_new(f, ir, n->type, n->s);
        list_append(ir->v, igen_node(f, n->sons[0]));
        break;
    case Nassign:
        ir->kind = Iset;
        list_append(ir->v, igen_address(f, n->sons[0]));
        list_append(ir->v, igen_node(f, n->sons[1]));
        break;
    case Nderef:
        ir->kind = Ideref;
        list_append(ir->v, igen_node(f, n->sons[0]));
        ir->dst = var_new(f, ir, n->type, NULL);
        break;
    case Naddr:
        free(ir);
        return igen_address(f, n->sons[0]);
    case Nid:
        assert(n->e && n->e->n && n->e->n->v);
        free(ir);
        ++n->e->n->v->uses;
        return n->e->n->v;
    case Nint:
        ir->kind = Iint;
        ir->s = string_clone(n->s);
        ir->dst = var_new(f, ir, builtin_types[Tint]->override, NULL);
        break;
    case Nmodule: case Ntypeof: case Nfun: case Narglist: case Narg:
    case Nsons: case Nptr: assert(0);
    }

    if (ir) {
        int i;
        list_append(f->sons, ir);
        for (i=0; i<list_len(ir->v); ++i) {
            assert(ir->v[i]);
            ++ir->v[i]->uses;
        }
        return ir->dst;
    } else return NULL;
}

static void igen_sons(Decl* f, Node* n) {
    int i;
    assert(n && n->kind > Nsons);
    for (i=0; i<list_len(n->sons); ++i)
        igen_node(f, n->sons[i]);
}

static Decl* igen_func(Node* n) {
    int i;
    assert(n->kind == Nfun);
    Decl* res = new(Decl);
    res->name = string_clone(n->s);
    res->id = decl_counter++;
    assert(n->sons[1]->kind == Narglist);
    for (i=0; i<list_len(n->sons[1]->sons); ++i) {
        Node* arg = n->sons[1]->sons[i];
        assert(arg->kind == Narg);
        Var* v = var_new(res, NULL, arg->type, arg->s);
        list_append(res->args, v);
        arg->v = v;
    }
    if (n->sons[0]) res->ret = n->sons[0]->type;
    igen_node(res, n->sons[2]);
    return res;
}

List(Decl*) igen(Node* n) {
    List(Decl*) res = NULL;
    int i, j, k;
    assert(n);

    for (i=0; i<list_len(n->sons); ++i) switch (n->sons[i]->kind) {
    case Nmodule:
        for (j=0; j<list_len(n->sons[i]->sons); ++j) {
            List(Decl*) t = igen(n->sons[i]->sons[j]);
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
