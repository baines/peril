#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include "sb.h"
#include "hashing.h"

#define INSO_IMPL
#include "inso_ht.h"

enum atom_t { A_NONE, A_SYM, A_LIST, A_XLIST, A_STR, A_NUM, A_OPAQUE };

struct atom {
    enum atom_t type;
    union {
        char*        str;
        intmax_t     num;
        intptr_t     val;
    };
    struct atom* cdr;
    struct atom* car;
    char*        sym;
};

struct var {
    char* name;
    //struct atom* value;
    intptr_t value;
};
inso_ht symtab;

static inline size_t var_hash(const struct var* v) {
    // XXX: 32 to 64 bit?
    return murmur2(v->name, strlen(v->name));
}

static inline bool var_cmp(const struct var* v, const char* name) {
    return strcmp(v->name, name) == 0;
}

static struct atom* adup(struct atom* in) {
    if(in == NULL)
        return NULL;

    struct atom* r = malloc(sizeof(*r));
    if(r == NULL) {
        perror("malloc");
        exit(1);
    }

    *r = *in;
    return r;
}

static void skipws(void) {
    int c;
    while((c = getchar()) != EOF) {
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        ungetc(c, stdin);
        break;
    }
}

static char* read_str() {
    sb(char) str = NULL;
    int escape = 0;
    int c;

    skipws();
    assert(getchar() == '"');

    // TODO: escape chars \n \r \t \\ etc

    while((c = getchar()) != EOF) {
        if(c == '\\') {
            escape = 1;
            continue;
        }

        if(c == '"' && !escape) {
            break;
        }

        sb_push(str, c);
        escape = 0;
    }

    sb_push(str, '\0');
    return str;
}

static char* read_sym() {
    sb(char) str = NULL;
    int escape = 0;
    int c;

    skipws();

    while((c = getchar()) != EOF) {
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ']' || c == ')') {
            ungetc(c, stdin);
            break;
        }
        sb_push(str, c);
    }

    sb_push(str, '\0');
    return str;
}

static intmax_t read_num() {
    char* str = read_sym();
    return strtoimax(str, NULL, 10);
}

intptr_t ext_call(void (*)(), struct atom*);
static struct atom* read_atom_list_inner(char, int);
static intptr_t eval_atom(struct atom* a);

static intptr_t eval_func(const char* sym, struct atom* args) {

    // builtins
    // TODO: add a lot more...
    if(strcmp(sym, "+") == 0) {
        assert(args);

        intptr_t sum = 0;
        for(struct atom* a = args; a; a = a->cdr) {
            sum += eval_atom(a);
        }

        return sum;

    } else if(strcmp(sym, "%") == 0) {
        assert(args);

        intptr_t a = eval_atom(args);
        intptr_t b = eval_atom(args->cdr);
        return a % b;

    } else if(strcmp(sym, "let") == 0) {
        assert(args);
        assert(args->type == A_SYM);

        struct var v = {
            .name = args->sym,
            .value = eval_atom(args->cdr),
        };

        size_t hash = murmur2(args->sym, strlen(args->sym));
        struct var* existing;
        if((existing = inso_ht_get(&symtab, hash, (inso_ht_cmp_fn)&var_cmp, args->sym))) {
            existing->value = v.value;
        } else {
            inso_ht_put(&symtab, &v);
        }

        return 0;
    } else if(strcmp(sym, "while") == 0) {
        struct atom* cond = args;
        struct atom* block = args->cdr;

        assert(block);
        assert(block->type == A_LIST);

        intptr_t val;
        while((val = eval_atom(cond))) {
            eval_atom(block);
        }

    } else if(strcmp(sym, "import") == 0) {
        assert(args->type == A_STR);
        dlerror();
        dlopen(args->str, RTLD_GLOBAL | RTLD_LAZY);
        const char* err = dlerror();
        if(err) {
            fprintf(stderr, "[import %s]: %s\n", args->str, err);
        }

        return 0;
    } else {
        void (*fn)() = dlsym(RTLD_DEFAULT, sym);
        if(fn == NULL) {
            fprintf(stderr, "func not found: %s\n", sym);
            exit(1);
        }

        // look up symbols in pretty dodgy way
        for(struct atom* a = args; a; a = a->cdr) {
            if(a->type == A_SYM) {
                size_t hash = murmur2(a->sym, strlen(a->sym));
                struct var* v = inso_ht_get(&symtab, hash, (inso_ht_cmp_fn)&var_cmp, a->sym);
                if(v == NULL) {
                    fprintf(stderr, "undefined symbol [%s]\n", a->sym);
                    exit(1);
                }
                a->val = v->value;
            } else if(a->type == A_XLIST) {
                a->val = eval_atom(a);
            }
        }

        return ext_call(fn, args);
    }
}

static intptr_t eval_atom(struct atom* a) {
    if(a == NULL)
        return 0;

    switch(a->type) {
        case A_NUM:
            return a->num;

        case A_STR:
            return (intptr_t)a->str;

        case A_OPAQUE:
            return a->val;

        case A_SYM: {
            size_t hash = murmur2(a->sym, strlen(a->sym));
            struct var* v = inso_ht_get(&symtab, hash, (inso_ht_cmp_fn)&var_cmp, a->sym);
            if(v == NULL) {
                // TODO: call func?
                return 0;
            }
            return v->value;
        } break;

        case A_LIST: {
            // XXX: does this make sense??
            intptr_t result = 0;
            for(struct atom* l = a->car; l; l = l->cdr) {
                result = eval_atom(l);
            }
            return result;
        } break;

        case A_XLIST: {
            struct atom* sym = a->car;
            assert(sym->type == A_SYM);
            return eval_func(sym->sym, sym->cdr);
        } break;
    }

    return 0;
}

static intptr_t eval(void) {
    const char* sym = read_sym();

    // XXX: big hack
    int sub_eval = 1;
    if(strcmp(sym, "while") == 0) {
        sub_eval = 0;
    }

    struct atom* args = read_atom_list_inner(')', sub_eval);

    return eval_func(sym, args);
}

static struct atom* read_atom_list(int run_eval);

static struct atom* read_atom(int run_eval) {
    struct atom a = {};

    skipws();
    int c = getchar();

    if(c == EOF) {
        return NULL;
    } else if(c == '"') {
        ungetc(c, stdin);
        a.type = A_STR;
        a.str = read_str();
    } else if(c == '[') {
        ungetc(c, stdin);
        a.type = A_LIST;
        a.car = read_atom_list(run_eval);
    } else if(c >= '0' && c <= '9' || c == '-') {
        ungetc(c, stdin);
        a.type = A_NUM;
        a.num = read_num();
    } else if(c == '(') {
        if(run_eval) {
            a.type = A_OPAQUE;
            a.val = eval();
        } else {
            a.type = A_XLIST;
            a.car = read_atom_list_inner(')', 0);
        }
    } else {
        ungetc(c, stdin);
        a.type = A_SYM;
        a.sym = read_sym();
    }

    return adup(&a);
}

static struct atom* read_atom_list_inner(char terminator, int run_eval) {
    int c;
    struct atom* head = NULL;
    struct atom** next = &head;

    while((c = getchar()) != EOF) {
        if(c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;

        if(c == terminator) {
            return adup(head);
        }

        ungetc(c, stdin);
        *next = read_atom(run_eval);
        if(*next == NULL) {
            goto error;
        }

        next = &(*next)->cdr;
    }

error:
    fprintf(stderr, "unterminated list\n");
    exit(1);
}

static struct atom* read_atom_list(int run_eval) {
    skipws();
    assert(getchar() == '[');
    return read_atom_list_inner(']', run_eval);
}

static void print_atom_list(struct atom* a, int depth);

static void print_atom(struct atom* a, int depth) {
    if(a == NULL) {
        puts("");
        return;
    }

    switch(a->type){
        case A_SYM: {
            fputs(a->sym, stdout);
        } break;

        case A_STR: {
            printf("\"%s\"", a->str);
        } break;

        case A_NUM: {
            printf("%" PRIiMAX, a->num);
        } break;

        case A_LIST:
        case A_XLIST: {
            printf("\n%*s[", depth, "");
            print_atom_list(a->car, depth+2);
            printf("]");
        } break;

        case A_OPAQUE: {
            printf("<%p>", (void*)a->val);
        } break;
    }
}

static void print_atom_list(struct atom* a, int depth) {
    print_atom(a, depth);
    while(a && (a = a->cdr)) {
        printf(" ");
        print_atom(a, depth);
    }
}

static void exitsig(int s) {
    exit(0);
}

int main(int argc, char** argv){

    inso_ht_init(&symtab, 32, sizeof(struct var), (inso_ht_hash_fn)&var_hash);
    signal(SIGINT, &exitsig);

    int dump = 0;

    int arg;
    while((arg = getopt(argc, argv, "d")) != -1) {
        if(arg == 'd')
            dump = 1;
    }

    do {
        struct atom* a = read_atom(!dump);
        if(dump) {
            print_atom(a, 0);
        }
    } while(!feof(stdin));
}
