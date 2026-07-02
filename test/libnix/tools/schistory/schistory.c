/*
 * schistory -- derive per-call @since..until version ranges for a syscall superset table by
 * diffing a family's sys/kern/syscalls.master across releases, and emit the annotated .sc that
 * sc2int consumes.
 *
 * Usage: schistory --family <name> --name "<human>" --bae <ERRNO> --out <file.sc> \
 *                  <ver>=<master> <ver>=<master> ...
 *
 * The releases may be given in any order -- schistory sorts them ascending by parsed version
 * before processing, so a CMake glob need not sort numerically. We track, per (number,name), the
 * first release it appears as a live STD/NOARGS/NODEF call (since) and the first release it is gone
 * or OBSOL/UNIMPL after having existed (until). BSD numbering is stable, so (number,name) is a
 * reliable identity; a renumber shows up as a new identity (new since).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCH_MAX_CALLS 1024
#define SCH_MAX_RELS  128
#define SCH_LINE      2048
#define SCH_NAME      128
#define SCH_PROTO     1024
#define SCH_VER       16

typedef struct _sch_call {
    int      number;
    char     name[SCH_NAME];
    char     rettype[SCH_NAME]; /* .sc return type keyword */
    char     format[SCH_PROTO]; /* comma-separated .sc arg type keywords */
    char     since[SCH_VER];    /* version string; "" = unset */
    char     until[SCH_VER];    /* version string; "" = never removed */
    int      present_now;       /* seen live in the release currently being parsed */
    int      ever;              /* has ever been live */
} sch_call_t;

typedef struct _sch_release {
    char version[SCH_VER];
    char path[1024];
} sch_release_t;

static sch_call_t    g_calls[SCH_MAX_CALLS];
static int           g_ncalls = 0;
static int           g_limit = 0;
static sch_release_t g_rels[SCH_MAX_RELS];
static int           g_nrels = 0;

/* Pack "MAJOR.MINOR[.PATCH]" the same way nix_version_t does, so ordering matches the runtime. */
static unsigned long
sch_version_pack(char const *v)
{
    unsigned major = 0, minor = 0, patch = 0;
    sscanf(v, "%u.%u.%u", &major, &minor, &patch);
    return ((unsigned long)(major & 0xFFFF) << 16) | ((unsigned long)(minor & 0xFF) << 8)
           | (unsigned long)(patch & 0xFF);
}

/* Map a C proto arg token to a single .sc type keyword. Pointers -> ptr; the classification below
 * is explicit (no magic): 64-bit-ish -> dword, pointer-width -> intptr, else word. */
static char const *
sch_type_of(char const *ctype, int is_pointer)
{
    if (is_pointer) { return "ptr"; }
    if (strstr(ctype, "off_t") || strstr(ctype, "int64") || strstr(ctype, "uint64")
        || strstr(ctype, "quad") || strstr(ctype, "long long")) {
        return "dword";
    }
    if (strstr(ctype, "size_t") || strstr(ctype, "ssize_t") || strstr(ctype, "intptr")
        || strstr(ctype, "long")) {
        return "intptr";
    }
    if (strstr(ctype, "double")) { return "double"; }
    if (strstr(ctype, "float")) { return "single"; }
    return "word";
}

/* Return the .sc return type for a C return-type spelling. */
static char const *
sch_rettype_of(char const *ret)
{
    if (strstr(ret, "void")) { return "void"; }
    if (strstr(ret, "off_t") || strstr(ret, "int64") || strstr(ret, "quad")) { return "dword"; }
    if (strchr(ret, '*')) { return "ptr"; }
    if (strstr(ret, "size_t") || strstr(ret, "long") || strstr(ret, "intptr")) { return "intptr"; }
    return "word";
}

static sch_call_t *
sch_find(int number, char const *name)
{
    int i;
    for (i = 0; i < g_ncalls; i++) {
        if (g_calls[i].number == number && strcmp(g_calls[i].name, name) == 0) {
            return &g_calls[i];
        }
    }
    return NULL;
}

/* Parse one "{ ret name(args); }" prototype (already stripped of braces) into a call record. */
static void
sch_parse_proto(char const *proto, int number, char const *version)
{
    char        ret[SCH_NAME] = { 0 };
    char        name[SCH_NAME] = { 0 };
    char const *lp = strchr(proto, '(');
    char const *rp = lp ? strrchr(lp, ')') : NULL;
    char const *p, *nstart;
    sch_call_t *c;
    char        args[SCH_PROTO];
    size_t      nlen, rlen, alen;

    if (lp == NULL || rp == NULL) { return; }

    /* name = the last identifier before '('; ret = everything before the name. */
    p = lp;
    while (p > proto && (isspace((unsigned char)p[-1]) || p[-1] == '(')) { p--; }
    nstart = p;
    while (nstart > proto && (isalnum((unsigned char)nstart[-1]) || nstart[-1] == '_')) { nstart--; }
    nlen = (size_t)(p - nstart);
    if (nlen == 0 || nlen >= SCH_NAME) { return; }
    memcpy(name, nstart, nlen);
    name[nlen] = '\0';
    rlen = (size_t)(nstart - proto);
    while (rlen > 0 && isspace((unsigned char)proto[rlen - 1])) { rlen--; }
    if (rlen >= SCH_NAME) { rlen = SCH_NAME - 1; }
    memcpy(ret, proto, rlen);
    ret[rlen] = '\0';

    c = sch_find(number, name);
    if (c == NULL) {
        if (g_ncalls >= SCH_MAX_CALLS) { return; }
        c = &g_calls[g_ncalls++];
        memset(c, 0, sizeof(*c));
        c->number = number;
        strncpy(c->name, name, SCH_NAME - 1);
        strncpy(c->since, version, SCH_VER - 1);
    }
    strncpy(c->rettype, sch_rettype_of(ret), SCH_NAME - 1);

    /* Build the .sc arg format from the inner arg list. An arg list that is exactly "void" (after
     * trimming) means no args -- do NOT match "void" as a substring, since a "void *" pointer
     * argument legitimately contains it. */
    c->format[0] = '\0';
    alen = (size_t)(rp - lp - 1);
    if (alen >= sizeof(args)) { alen = sizeof(args) - 1; }
    memcpy(args, lp + 1, alen);
    args[alen] = '\0';
    {
        size_t lead = 0;
        while (args[lead] != '\0' && isspace((unsigned char)args[lead])) { lead++; }
        if (lead > 0) { memmove(args, args + lead, strlen(args + lead) + 1); }
        while (args[0] != '\0' && isspace((unsigned char)args[strlen(args) - 1])) {
            args[strlen(args) - 1] = '\0';
        }
    }
    if (args[0] != '\0' && strcmp(args, "void") != 0) {
        char *save = NULL;
        char *tok;
        for (tok = strtok_r(args, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
            int is_ptr = (strchr(tok, '*') != NULL);
            if (c->format[0] != '\0') {
                strncat(c->format, ", ", sizeof(c->format) - strlen(c->format) - 1);
            }
            strncat(c->format, sch_type_of(tok, is_ptr), sizeof(c->format) - strlen(c->format) - 1);
        }
    }

    c->present_now = 1;
    c->ever = 1;
    if (number + 1 > g_limit) { g_limit = number + 1; }
}

/* Parse one release's master; mark presence; close ranges for calls that vanished. */
static void
sch_parse_release(char const *path, char const *version)
{
    FILE *f = fopen(path, "r");
    char  line[SCH_LINE];
    int   i;

    if (f == NULL) {
        fprintf(stderr, "schistory: cannot open %s\n", path);
        exit(2);
    }
    for (i = 0; i < g_ncalls; i++) { g_calls[i].present_now = 0; }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        int   number;
        char *brace;
        while (isspace((unsigned char)*p)) { p++; }
        if (*p == ';' || *p == '#' || *p == '\0') { continue; }
        if (!isdigit((unsigned char)*p)) { continue; }
        number = atoi(p);
        /* STD/NOARGS/NODEF/NOERR with a { proto } are live calls; OBSOL/UNIMPL/EXCL are gaps. */
        brace = strchr(p, '{');
        if (brace != NULL
            && (strstr(p, "STD") || strstr(p, "NOARGS") || strstr(p, "NODEF") || strstr(p, "NOERR"))) {
            char *end = strrchr(brace, '}');
            if (end != NULL) { *end = '\0'; }
            sch_parse_proto(brace + 1, number, version);
        }
        /* OBSOL/UNIMPL: leave present_now = 0 so the range closes below. */
    }
    fclose(f);

    /* Any call that existed but is absent here, with no until yet, ends at this version. */
    for (i = 0; i < g_ncalls; i++) {
        if (g_calls[i].ever && !g_calls[i].present_now && g_calls[i].until[0] == '\0') {
            strncpy(g_calls[i].until, version, SCH_VER - 1);
        }
    }
}

static int
sch_cmp_call(void const *a, void const *b)
{
    return ((sch_call_t const *)a)->number - ((sch_call_t const *)b)->number;
}

static int
sch_cmp_rel(void const *a, void const *b)
{
    unsigned long va = sch_version_pack(((sch_release_t const *)a)->version);
    unsigned long vb = sch_version_pack(((sch_release_t const *)b)->version);
    return (va > vb) - (va < vb);
}

int
main(int argc, char **argv)
{
    char const *family = "unknown";
    char const *human = "Unknown";
    char const *bae = "EFAULT";
    char const *out = NULL;
    int         i;
    FILE       *o;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--family") && i + 1 < argc) {
            family = argv[++i];
        } else if (!strcmp(argv[i], "--name") && i + 1 < argc) {
            human = argv[++i];
        } else if (!strcmp(argv[i], "--bae") && i + 1 < argc) {
            bae = argv[++i];
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out = argv[++i];
        } else {
            char *eq = strchr(argv[i], '=');
            if (eq != NULL && g_nrels < SCH_MAX_RELS) {
                *eq = '\0';
                strncpy(g_rels[g_nrels].version, argv[i], SCH_VER - 1);
                strncpy(g_rels[g_nrels].path, eq + 1, sizeof(g_rels[g_nrels].path) - 1);
                g_nrels++;
            }
        }
    }
    if (out == NULL) {
        fprintf(stderr, "schistory: --out required\n");
        return 2;
    }

    /* Process releases in ascending version order regardless of argv order. */
    qsort(g_rels, (size_t)g_nrels, sizeof(g_rels[0]), sch_cmp_rel);
    for (i = 0; i < g_nrels; i++) {
        sch_parse_release(g_rels[i].path, g_rels[i].version);
    }

    qsort(g_calls, (size_t)g_ncalls, sizeof(g_calls[0]), sch_cmp_call);

    o = fopen(out, "w");
    if (o == NULL) {
        fprintf(stderr, "schistory: cannot write %s\n", out);
        return 2;
    }
    fprintf(o, "# Generated by schistory from vendored syscalls.master history. DO NOT EDIT.\n");
    fprintf(o, "# family: %s (%d releases, %d calls)\n", family, g_nrels, g_ncalls);
    fprintf(o, "NAME: \"%s\"\n", human);
    fprintf(o, "NAMESPACE: %s\n", family);
    fprintf(o, "LIMIT: %d\n", g_limit);
    fprintf(o, "BAE: %s\n\n", bae);
    fprintf(o, "CALLS\n\n");
    for (i = 0; i < g_ncalls; i++) {
        sch_call_t *c = &g_calls[i];
        fprintf(o, "%d %s %s (%s)", c->number, c->rettype, c->name, c->format);
        if (c->since[0] != '\0') {
            fprintf(o, " @%s", c->since);
            if (c->until[0] != '\0') { fprintf(o, "..%s", c->until); }
        }
        fprintf(o, "\n");
    }
    fclose(o);
    return 0;
}
