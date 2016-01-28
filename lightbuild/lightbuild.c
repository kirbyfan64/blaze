#include <openssl/sha.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

typedef struct File File;
typedef struct Options Options;

struct File {
    char* path, *obj;
    int dirty;
};

struct Options {
    char* compiler, *cflags, *lflags, *target, *objflag;
    size_t compiler_l, cflags_l, lflags_l, target_l, objflag_l;
};

static void fatal(const char* msg, const char* file) {
    fprintf(stderr, "fatal error%s%s: %s\n", file==NULL?"":" ",
            file==NULL?"":file, msg);
    exit(EXIT_SUCCESS);
}

static void* alloc(size_t sz) {
    void* res = calloc(sz, 1);
    if (!res) fatal("out of memory", NULL);
    return res;
}

typedef void* (*spawn_func)(void* ptr);

#ifdef HAVE_PTHREAD_H
#include <pthread.h>

typedef pthread_t Thread;

static void spawn(spawn_func f, Thread* t, void* arg) {
    int err;
    if ((err = pthread_create(t, NULL, f, arg)))
        fatal(strerror(err), NULL);
}

static void stop(Thread t) {
    void* ret;
    assert(pthread_cancel(t) == 0);
    assert(pthread_join(t, &ret) == 0);
    assert(ret == PTHREAD_CANCELED);
}

static void thread_setup() {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
}

#define MAX_PROCESSING 10
#else
#error TODO
#define MAX_PROCESSING 1
#endif

Thread threads[MAX_PROCESSING];
File* all_files;
int nfiles;
File* processing[MAX_PROCESSING];

#ifdef __GNUC__

#ifdef __ATOMIC_SEQ_CST
#define atomic_load(x) __atomic_load_n(x, __ATOMIC_SEQ_CST)
#define atomic_store(x,v) __atomic_store_n(x, v, __ATOMIC_SEQ_CST)
#define queue_test(n,f) do {\
    File* expected = NULL;\
    if (__atomic_compare_exchange_n(&processing[n], &expected, f, 0,\
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;\
} while (0)
#else
#error TODO
#endif

#else

#error TODO

#endif

static void queue_file(File* f) {
    int i;
    for (;;) for (i=0; i<MAX_PROCESSING; ++i) queue_test(i, f);
}

static void wait_all() {
    int i, cont;
    for (;;) {
        cont = 0;
        for (i=0; i<MAX_PROCESSING; ++i)
            if (atomic_load(&processing[i])) {
                cont = 1;
                break;
            }
        if (!cont) {
            for (i=0; i<MAX_PROCESSING; ++i) stop(threads[i]);
            return;
        }
    }
}

static Options opts;

static void parse(const char* path) {
    char buf[128];
    int i=0;
    FILE* f;
    memset(&opts, 0, sizeof(Options));

    if (!(f = fopen(path, "r"))) fatal(strerror(errno), path);

    while (fgets(buf, sizeof(buf), f)) {
        size_t bufsz = strlen(buf);
        if (buf[bufsz-1] == '\n') buf[--bufsz] = 0;
        if (bufsz == 0) continue;
        if (all_files) {
            all_files[i].path = alloc(bufsz+1);
            memcpy(all_files[i].path, buf, bufsz+1);
            all_files[i].obj = alloc(bufsz+3); // 1 + ".o"
            memcpy(all_files[i].obj, all_files[i].path, bufsz);
            memcpy(all_files[i].obj+bufsz, ".o", 3);
            ++i;
        } else switch (buf[0]) {
        #define C(c,t)\
        case c:\
            opts.t = alloc(bufsz+1);\
            memcpy(opts.t, buf+1, bufsz);\
            opts.t##_l = bufsz-1;\
            break;
        C('C', compiler)
        C('F', cflags)
        C('L', lflags)
        C('T', target)
        C('O', objflag)
        #undef C
        case ':':
            nfiles = atoi(buf+1);
            assert(nfiles);
            all_files = alloc(nfiles*sizeof(File));
            break;
        default:
            fprintf(stderr, "warning: unrecognized line opener %c\n", buf[0]);
            break;
        }
    }

    if (!feof(f)) fatal(strerror(errno), path);
    fclose(f);

    assert(all_files);
    assert(opts.compiler);
    assert(opts.cflags);
    assert(opts.lflags);
    assert(opts.target);
    assert(opts.objflag);
}

static void hash(unsigned char* tgt, const char* path) {
    char buf[1024];
    SHA_CTX ctx;
    size_t read;
    FILE* f = fopen(path, "r");
    if (f == NULL) fatal(strerror(errno), path);

    SHA1_Init(&ctx);
    while ((read = fread(buf, 1, sizeof(buf), f)))
        SHA1_Update(&ctx, buf, read);
    if (!feof(f)) fatal(strerror(errno), path);
    SHA1_Final(tgt, &ctx);
}

static int is_dirty(const char* path) {
    int dirty = 0;
    unsigned char curhash[SHA_DIGEST_LENGTH], oldhash[SHA_DIGEST_LENGTH];
    FILE* f;
    size_t l;
    char* p2;

    hash(curhash, path);

    l = strlen(path);
    p2 = alloc(l+6); // 1 + ".hash"
    memcpy(p2, path, l);
    memcpy(p2+l, ".hash", 6);

    f = fopen(p2, "r");
    if (f) {
        fread(oldhash, 1, SHA_DIGEST_LENGTH, f);
        fclose(f);
        if (memcmp(curhash, oldhash, SHA_DIGEST_LENGTH) != 0) dirty = 1;
    } else dirty = 1;

    if (dirty) {
        f = fopen(p2, "w");
        if (!f) fatal(strerror(errno), p2);
        fwrite(curhash, 1, SHA_DIGEST_LENGTH, f);
        fclose(f);
    }

    free(p2);

    return dirty;
}

static void* dirty_thread(void* fv) {
    File** ptr = fv;
    File* f = NULL;
    thread_setup();
    for (;;) {
        while (!(f = atomic_load(ptr)));
        f->dirty = is_dirty(f->path);
        atomic_store(ptr, NULL);
    }
}

static void dirty_tests() {
    int i;
    for (i=0; i<MAX_PROCESSING; ++i)
        spawn(dirty_thread, &threads[i], &processing[i]);
    for (i=0; i<nfiles; ++i) queue_file(&all_files[i]);
    wait_all();

    for (i=0; i<nfiles; ++i)
        printf("%s: %d\n", all_files[i].path, all_files[i].dirty);
}

static void compile(File* f) {
    char* buf;
    size_t total = 0, pathlen = strlen(f->path), objlen = pathlen + 2;
    buf = alloc(opts.compiler_l + 1 + opts.cflags_l + 1 + pathlen + 1 +
                opts.objflag_l + 1 + objlen + 1);
    #define P(s,l,c) memcpy(buf+total, s, l); total += l; buf[total++] = c;
    P(opts.compiler, opts.compiler_l, ' ')
    P(opts.cflags, opts.cflags_l, ' ')
    P(f->path, pathlen, ' ')
    P(opts.objflag, opts.objflag_l, ' ')
    P(f->obj, objlen, 0)
    puts(buf);
    free(buf);
}

static void link() {
    ;
}

static void build() {
    int i;

    for (i=0; i<nfiles; ++i)
        if (all_files[i].dirty) compile(&all_files[i]);
    link();
}

static void free_all() {
    int i;
    for (i=0; i<nfiles; ++i) free(all_files[i].path);
    free(all_files);
    free(opts.compiler);
    free(opts.cflags);
    free(opts.lflags);
    free(opts.target);
    free(opts.objflag);
}

int main(int argc, char** argv) {
    if (argc != 2) fatal("usage: lightbuild <build script>", NULL);
    parse(argv[1]);
    dirty_tests();
    build();
    free_all();
}
