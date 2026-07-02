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

/* A call from a --filter seed .sc: the already-IMPLEMENTED set, whose rettype/format we preserve
 * verbatim so the sc2int-generated callbacks keep matching the hand-written implementations. */
typedef struct _sch_seedcall {
    int  number;
    char name[SCH_NAME];
    char rettype[SCH_NAME];
    char format[SCH_PROTO];
} sch_seedcall_t;

static sch_call_t    g_calls[SCH_MAX_CALLS];
static int           g_ncalls = 0;
static int           g_limit = 0;
static sch_release_t g_rels[SCH_MAX_RELS];
static int           g_nrels = 0;
static sch_seedcall_t g_seed[SCH_MAX_CALLS];
static int            g_nseed = 0;
static char           g_seed_name[SCH_PROTO] = "";
static char           g_seed_ns[SCH_NAME] = "";
static char           g_seed_bae[SCH_NAME] = "";
static int            g_seed_limit = 0;

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

/* Canonicalize a syscall name across BSD-format evolution: strip a leading "sys_" (OpenBSD and
 * NetBSD 2.x-5.x spell calls sys_<name>; the modern NetBSD "rettype|sys||<name>" form already
 * yields <name> from the identifier walk). */
static char const *
sch_canon_name(char const *name)
{
    if (strncmp(name, "sys_", 4) == 0) { return name + 4; }
    return name;
}

/* Find or create the (number, canonical-name) call record; mark it present in the current release
 * and, once ever seen, tracked for range closing. Returns the record. */
static sch_call_t *
sch_touch(int number, char const *rawname, char const *version)
{
    char const *name = sch_canon_name(rawname);
    sch_call_t *c = sch_find(number, name);
    if (c == NULL) {
        if (g_ncalls >= SCH_MAX_CALLS) { return NULL; }
        c = &g_calls[g_ncalls++];
        memset(c, 0, sizeof(*c));
        c->number = number;
        strncpy(c->name, name, SCH_NAME - 1);
        strncpy(c->since, version, SCH_VER - 1);
    }
    c->present_now = 1;
    c->ever = 1;
    if (number + 1 > g_limit) { g_limit = number + 1; }
    return c;
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

    /* The name lives on the FIRST line, before '(' -- so we can record it even when the prototype
     * is truncated by a '\' continuation (rp == NULL); only the arg format needs the full proto. */
    if (lp == NULL) { return; }

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

    c = sch_touch(number, name, version);
    if (c == NULL) { return; }
    strncpy(c->rettype, sch_rettype_of(ret), SCH_NAME - 1);

    /* A truncated ('\'-continued) prototype has no ')': the name is recorded, the format is not. */
    if (rp == NULL) { return; }

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
}

/* The pre-brace columnar NetBSD format ("1 STD 1 exit"): the syscall name is the last
 * whitespace-delimited token on the line. Record its name-identity (no proto). */
static void
sch_parse_old_line(char *line, int number, char const *version)
{
    char *tok, *save = NULL, *last = NULL;
    for (tok = strtok_r(line, " \t\r\n", &save); tok != NULL; tok = strtok_r(NULL, " \t\r\n", &save)) {
        last = tok;
    }
    if (last != NULL && (isalpha((unsigned char)last[0]) || last[0] == '_')) {
        sch_touch(number, last, version);
    }
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
        if (strstr(p, "STD") == NULL && strstr(p, "NOARGS") == NULL && strstr(p, "NODEF") == NULL
            && strstr(p, "NOERR") == NULL) {
            /* OBSOL/UNIMPL/EXCL/INDIR: not a live call -- leave present_now = 0 so the range closes. */
            continue;
        }
        if (brace != NULL) {
            char *end = strrchr(brace, '}');
            if (end != NULL) { *end = '\0'; }
            sch_parse_proto(brace + 1, number, version);
        } else {
            /* Pre-brace columnar NetBSD format ("1 STD 1 exit"). */
            sch_parse_old_line(p, number, version);
        }
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

/* Parse a seed .sc: capture the header (NAME/NAMESPACE/LIMIT/BAE) and every call's number, name,
 * rettype and raw "(...)" format string, so filter mode can re-emit them verbatim plus ranges. */
static void
sch_parse_seed(char const *path)
{
    FILE *f = fopen(path, "r");
    char  line[SCH_LINE];

    if (f == NULL) {
        fprintf(stderr, "schistory: cannot open seed %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        while (isspace((unsigned char)*p)) { p++; }
        if (*p == '#' || *p == '\0') { continue; }
        if (!strncmp(p, "NAME:", 5)) {
            char *q = strchr(p, '"');
            char *e = q ? strchr(q + 1, '"') : NULL;
            if (q && e) {
                size_t n = (size_t)(e - q - 1);
                if (n >= sizeof(g_seed_name)) { n = sizeof(g_seed_name) - 1; }
                memcpy(g_seed_name, q + 1, n);
                g_seed_name[n] = '\0';
            }
        } else if (!strncmp(p, "NAMESPACE:", 10)) {
            sscanf(p + 10, " %127s", g_seed_ns);
        } else if (!strncmp(p, "BAE:", 4)) {
            sscanf(p + 4, " %127s", g_seed_bae);
        } else if (!strncmp(p, "LIMIT:", 6)) {
            g_seed_limit = atoi(p + 6);
        } else if (isdigit((unsigned char)*p)) {
            /* "N rettype name (fmt)" -- capture verbatim. */
            sch_seedcall_t *s;
            char            rettype[SCH_NAME] = { 0 };
            char            name[SCH_NAME] = { 0 };
            int             number = 0;
            char           *lp, *rp;
            if (sscanf(p, "%d %127s %127s", &number, rettype, name) != 3) { continue; }
            if (g_nseed >= SCH_MAX_CALLS) { continue; }
            s = &g_seed[g_nseed++];
            memset(s, 0, sizeof(*s));
            s->number = number;
            strncpy(s->name, name, SCH_NAME - 1);
            strncpy(s->rettype, rettype, SCH_NAME - 1);
            lp = strchr(p, '(');
            rp = lp ? strchr(lp, ')') : NULL;
            if (lp && rp && rp > lp + 1) {
                size_t n = (size_t)(rp - lp - 1);
                if (n >= sizeof(s->format)) { n = sizeof(s->format) - 1; }
                memcpy(s->format, lp + 1, n);
                s->format[n] = '\0';
            }
        }
    }
    fclose(f);
}

int
main(int argc, char **argv)
{
    char const *family = "unknown";
    char const *human = "Unknown";
    char const *bae = "EFAULT";
    char const *out = NULL;
    char const *filter = NULL;
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
        } else if (!strcmp(argv[i], "--filter") && i + 1 < argc) {
            filter = argv[++i];
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

    if (filter != NULL) {
        /* Filter mode: emit exactly the seed's IMPLEMENTED calls (rettype/format verbatim, so the
         * generated callbacks keep matching the hand-written implementations), each annotated with
         * the @since..until derived from the masters. A seed call absent from the sampled masters
         * gets no range (always available -- the safe default for an implemented call). */
        sch_parse_seed(filter);
        fprintf(o, "# filter: %s (%d implemented calls) x %d masters\n", filter, g_nseed, g_nrels);
        fprintf(o, "NAME: \"%s\"\n", g_seed_name[0] ? g_seed_name : human);
        fprintf(o, "NAMESPACE: %s\n", g_seed_ns[0] ? g_seed_ns : family);
        fprintf(o, "LIMIT: %d\n", g_seed_limit);
        fprintf(o, "BAE: %s\n\n", g_seed_bae[0] ? g_seed_bae : bae);
        fprintf(o, "CALLS\n\n");
        for (i = 0; i < g_nseed; i++) {
            sch_seedcall_t *s = &g_seed[i];
            sch_call_t     *c = sch_find(s->number, s->name);
            fprintf(o, "%d %s %s (%s)", s->number, s->rettype, s->name, s->format);
            if (c != NULL && c->since[0] != '\0') {
                fprintf(o, " @%s", c->since);
                if (c->until[0] != '\0') { fprintf(o, "..%s", c->until); }
            }
            fprintf(o, "\n");
        }
        fclose(o);
        return 0;
    }

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
