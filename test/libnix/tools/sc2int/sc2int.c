#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nix-syscall.h"
#include "sc2int.h"

#define APPNAME "sc2int"

char const  *g_filename = NULL;
unsigned     g_line = 1;
char        *g_gbl_name = NULL;
char        *g_gbl_bae = NULL;
char        *g_gbl_ns = NULL;
char        *g_gbl_NS = NULL;
int          g_gbl_limit = 0;
call_list_t *g_calls = NULL;

#define BYTE_SIZE 8
int guest_word_size = 32;

static char const *
get_ctype_for_param(param_t *p)
{
	int dsize;
	int bytebits = BYTE_SIZE;

	if (p == NULL || p->ellipsis)
		return NULL;
	else
		switch (p->type) {
		case NIX_PARAM_POINTER:
			return "void *";
		case NIX_PARAM_BYTE:
			dsize = (bytebits * 1);
			break;
		case NIX_PARAM_HALF:
			dsize = (bytebits * 2);
			break;
		case NIX_PARAM_WORD:
			dsize = (bytebits * 4);
			break;
		case NIX_PARAM_DWORD:
			dsize = (bytebits * 8);
			break;
		case NIX_PARAM_SINGLE:
			dsize = (bytebits * 4);
			break;
		case NIX_PARAM_DOUBLE:
			dsize = (bytebits * 8);
			break;
		case NIX_PARAM_EXTENDED:
			dsize = (bytebits * 16);
			break;
		case NIX_PARAM_INTPTR:
			dsize = guest_word_size;
			break;
		default:
			LCAssert0(0);
		}

	nix_param_t xp;
	LCAssert0(dsize > 0 && dsize <= (int)(sizeof(xp.value.tnosign.u64) << 3));
	switch (p->type) {
	case NIX_PARAM_BYTE:
	case NIX_PARAM_HALF:
	case NIX_PARAM_WORD:
	case NIX_PARAM_DWORD:
	case NIX_PARAM_INTPTR:
		if (dsize <= 8)
			return "uint8_t";
		else if (dsize <= 16)
			return "uint16_t";
		else if (dsize <= 32)
			return "uint32_t";
		else if (dsize <= 64)
			return "uint64_t";
		else
			LCAssert0(0);
		break;

	case NIX_PARAM_SINGLE:
	case NIX_PARAM_DOUBLE:
	case NIX_PARAM_EXTENDED:
		if (dsize <= 32)
			return "float";
		else if (dsize <= 64)
			return "double";
		else if (dsize <= 128)
			return "long double";
		else
			LCAssert0(0);
		break;

	default:
		LCAssert0(0);
	}

	LCAssert0(0);
	return NULL;
}

static char *
uppercase(char const *s)
{
	size_t n;
	char  *r;

	if (s == NULL)
		return NULL;

	r = (char *)malloc(strlen(s) + 1);
	for (n = 0; n < strlen(s); n++)
		r[n] = toupper(s[n]);
	r[strlen(s)] = 0;

	return r;
}

static char *
stringify(char const *s)
{
	if (s == NULL)
		return strdup("NULL");
	else {
		char *p = (char *)malloc(strlen(s) + 3);
		LCAssert0(p != NULL);
		if (p != NULL) {
			snprintf(p, strlen(s) + 3, "\"%s\"", s);
		}
		return p;
	}
}

static char *
call_get_param_format(param_t *p)
{
	char buf[4], *bp = buf;

	if (p != NULL) {
		*bp++ = '"';

		if (p->ellipsis) {
			*bp++ = '*';
		} else
			switch (p->type) {
			case NIX_PARAM_BYTE:
				*bp++ = 'b';
				break;
			case NIX_PARAM_HALF:
				*bp++ = 'h';
				break;
			case NIX_PARAM_WORD:
				*bp++ = 'w';
				break;
			case NIX_PARAM_DWORD:
				*bp++ = 'd';
				break;
			case NIX_PARAM_SINGLE:
				*bp++ = 'S';
				break;
			case NIX_PARAM_DOUBLE:
				*bp++ = 'D';
				break;
			case NIX_PARAM_EXTENDED:
				*bp++ = 'X';
				break;
			case NIX_PARAM_VECTOR:
				*bp++ = 'v';
				break;
			case NIX_PARAM_POINTER:
				*bp++ = 'p';
				break;
			case NIX_PARAM_INTPTR:
				*bp++ = 'l';
				break;
			default:
				LCAssert0(0);
				break;
			}

		*bp++ = '"';

		*bp = 0;
	}

	if (bp - buf <= 2)
		return strdup("NULL");
	else
		return strdup(buf);
}

static char *
call_get_params_format(param_list_t *pl)
{
	param_t *p;
	char     buf[32], *bp = buf;

	if (pl != NULL) {
		*bp++ = '"';

		TAILQ_FOREACH (p, pl, link) {
			if (p->ellipsis) {
				*bp++ = '*';
				break;
			} else
				switch (p->type) {
				case NIX_PARAM_BYTE:
					*bp++ = 'b';
					break;
				case NIX_PARAM_HALF:
					*bp++ = 'h';
					break;
				case NIX_PARAM_WORD:
					*bp++ = 'w';
					break;
				case NIX_PARAM_DWORD:
					*bp++ = 'd';
					break;
				case NIX_PARAM_SINGLE:
					*bp++ = 'S';
					break;
				case NIX_PARAM_DOUBLE:
					*bp++ = 'D';
					break;
				case NIX_PARAM_EXTENDED:
					*bp++ = 'X';
					break;
				case NIX_PARAM_VECTOR:
					*bp++ = 'v';
					break;
				case NIX_PARAM_POINTER:
					*bp++ = 'p';
					break;
				case NIX_PARAM_INTPTR:
					*bp++ = 'l';
					break;
				default:
					LCAssert0(0);
					break;
				}
		}

		*bp++ = '"';

		*bp = 0;
	}

	if (bp - buf <= 2)
		return strdup("NULL");
	else
		return strdup(buf);
}

static bool
param_list_has_variadic(param_list_t *pl)
{
	if (pl != NULL) {
		param_t *p;

		TAILQ_FOREACH (p, pl, link) {
			if (p->ellipsis)
				return true;
		}
	}

	return false;
}

static bool
param_list_has_pointer(param_list_t *pl)
{
	if (pl != NULL) {
		param_t *p;

		TAILQ_FOREACH (p, pl, link) {
			if (!p->ellipsis && p->type == NIX_PARAM_POINTER)
				return true;
		}
	}

	return false;
}

static char *
call_get_flags(call_t const *c)
{
	if (param_list_has_variadic(c->params))
		return strdup("NIX_US_SYSCALL_VARIADIC");
	else
		return strdup("0");
}

/*
 * Emit a nix_version_t initializer from a raw ".sc" version string.  The descriptor table is a
 * static const array, so the initializer must be a CONSTANT EXPRESSION: parse "MAJOR.MINOR[.PATCH]"
 * here and emit the NIX_VERSION(maj,min,pat) macro (NULL -> NIX_VERSION_NONE).
 */
static void
emit_version(FILE *out, char const *ver)
{
	unsigned major = 0, minor = 0, patch = 0;

	if (ver == NULL) {
		fprintf(out, "NIX_VERSION_NONE");
		return;
	}
	sscanf(ver, "%u.%u.%u", &major, &minor, &patch);
	fprintf(out, "NIX_VERSION (%u, %u, %u)", major, minor, patch);
}

static void
call_emit(FILE         *out,
          call_t const *call)
{
	if (call == NULL) {
		fprintf(out,
		        "{ %u, %s, %s, %s, %s, %u, NULL, NIX_VERSION_NONE, NIX_VERSION_NONE }",
		        0,
		        "NULL",
		        "NULL",
		        "NULL",
		        "0",
		        0);
	} else {
		int   nparams = 0;
		char *scname = NULL;
		char *name = stringify(call->name);
		char *format = call_get_params_format(call->params);
		char *rettype = call_get_param_format(call->rettype);
		char *flags = call_get_flags(call);

		if (call->params != NULL) {
			nparams = param_list_count(call->params);
			if (param_list_has_variadic(call->params))
				--nparams;
		}

		scname = (char *)malloc(strlen(g_gbl_NS) + strlen(call->name) + 16);
		sprintf(scname, "%s_SYS_%s", g_gbl_NS, call->name);

		fprintf(out,
		        "{ %s, %s, %s, %s, %s, %u, __%s_%s_callback, ",
		        scname,
		        name,
		        format,
		        rettype,
		        flags,
		        nparams,
		        g_gbl_ns,
		        call->name);
		emit_version(out, call->since);
		fprintf(out, ", ");
		emit_version(out, call->until);
		fprintf(out, " }");

		free(flags);
		free(rettype);
		free(format);
		free(name);
		free(scname);
	}
}

static char const *
get_param_type_name(param_t const *p)
{
	if (p == NULL || p->ellipsis)
		return "NIX_PARAM_INVALID";
	else
		switch (p->type) {
		case NIX_PARAM_BYTE:
			return "NIX_PARAM_BYTE";
		case NIX_PARAM_HALF:
			return "NIX_PARAM_HALF";
		case NIX_PARAM_WORD:
			return "NIX_PARAM_WORD";
		case NIX_PARAM_DWORD:
			return "NIX_PARAM_DWORD";
		case NIX_PARAM_SINGLE:
			return "NIX_PARAM_SINGLE";
		case NIX_PARAM_DOUBLE:
			return "NIX_PARAM_DOUBLE";
		case NIX_PARAM_EXTENDED:
			return "NIX_PARAM_EXTENDED";
		case NIX_PARAM_VECTOR:
			return "NIX_PARAM_VECTOR";
		case NIX_PARAM_POINTER:
			return "NIX_PARAM_POINTER";
		case NIX_PARAM_INTPTR:
			return "NIX_PARAM_INTPTR";
		default:
			return "NIX_PARAM_INVALID";
		}
}

static char const *
get_param_type_size_name(param_t const *p)
{
	if (p == NULL)
		return "void";
	else if (p->ellipsis)
		return "...";

	switch (p->type) {
	case NIX_PARAM_BYTE:
		return "byte";
	case NIX_PARAM_HALF:
		return "half";
	case NIX_PARAM_WORD:
		return "word";
	case NIX_PARAM_DWORD:
		return "dword";
	case NIX_PARAM_SINGLE:
		return "single";
	case NIX_PARAM_DOUBLE:
		return "double";
	case NIX_PARAM_EXTENDED:
		return "extended";
	case NIX_PARAM_VECTOR:
		return "vector";
	case NIX_PARAM_POINTER:
		return "ptr";
	case NIX_PARAM_INTPTR:
		return "intptr";
	default:
		LCAssert0(0);
		return NULL;
		break;
	}
}

static void
dump_def_comment(FILE         *out,
                 call_t const *call)
{
	fprintf(out,
	        "/* syscall: \"%s\" ret: \"%s\" args: ",
	        call->name,
	        get_param_type_size_name(call->rettype));

	if (call->params != NULL) {
		param_t *p;

		TAILQ_FOREACH (p, call->params, link)
			fprintf(out, "\"%s\" ", get_param_type_size_name(p));
	}

	fprintf(out, "*/\n");
}

static void
dump_defs(FILE    *out,
          call_t **calls)
{
	size_t n, m;

	fprintf(out, "/* %s System Calls Definitions */\n\n", g_gbl_name);

	fprintf(out, "#define %s_SYS_last %d\n\n", g_gbl_NS, g_gbl_limit);

	for (n = m = 0; n < (size_t)g_gbl_limit; n++) {
		if (calls[n] != NULL) {
			if (m++ > 0)
				fprintf(out, "\n");
			dump_def_comment(out, calls[n]);
			fprintf(out, "#define %s_SYS_%s %d\n", g_gbl_NS, calls[n]->name, calls[n]->scno);
		}
	}
}

static void
dump_cb_protos(FILE    *out,
               call_t **calls)
{
	size_t n;
	for (n = 0; n < (size_t)g_gbl_limit; n++) {
		if (calls[n] != NULL) {
			fprintf(out,
			        "static int __%s_%s_callback "
			        "(nix_us_syscall_if_t *, "
			        "nix_monitor_t *, "
			        "nix_param_t const *, "
			        "nix_param_t *);\n",
			        g_gbl_ns,
			        calls[n]->name);
		}
	}
}

static void
dump_func_protos(FILE    *out,
                 call_t **calls)
{
	size_t n;
	for (n = 0; n < (size_t)g_gbl_limit; n++) {
		if (calls[n] != NULL) {
			fprintf(out,
			        "extern int %s_%s "
			        "(nix_us_syscall_if_t *,"
			        "nix_monitor_t *, "
			        "%s_%s_args_t const *, "
			        "%s_%s_result_t *);\n",
			        g_gbl_ns,
			        calls[n]->name,
			        g_gbl_ns,
			        calls[n]->name,
			        g_gbl_ns,
			        calls[n]->name);
		}
	}
}

static void
dump_func_decls(FILE    *out,
                call_t **calls)
{
	size_t n;
	for (n = 0; n < (size_t)g_gbl_limit; n++) {
		if (calls[n] != NULL) {
			fprintf(out,
			        "int %s_%s (\n"
			        "  nix_us_syscall_if_t *xus,\n"
			        "  nix_monitor_t *xmon,\n"
			        "  %s_%s_args_t const *args,\n"
			        "  %s_%s_result_t *result)\n",
			        g_gbl_ns,
			        calls[n]->name,
			        g_gbl_ns,
			        calls[n]->name,
			        g_gbl_ns,
			        calls[n]->name);
			fprintf(out, "{\n\n");
			fprintf(out, "  /* Insert here implementation code */\n\n");
			fprintf(out, "  return 0;\n");
			fprintf(out, "}\n\n");
		}
	}
}

static void
dump_typedef(FILE   *out,
             call_t *call)
{
	if (call->rettype == NULL)
		fprintf(out, "#define %s_%s_result_t void\n", g_gbl_ns, call->name);
	else
		fprintf(out,
		        "typedef %s %s_%s_result_t;\n",
		        get_ctype_for_param(call->rettype),
		        g_gbl_ns,
		        call->name);
	fprintf(out, "typedef struct __%s_%s_args\n", g_gbl_ns, call->name);
	fprintf(out, "  {\n");
	if (call->params == NULL || (param_list_count(call->params) == 1 && param_list_has_variadic(call->params)))
		fprintf(out, "    int dummy;\n");
	else {
		param_t *p;
		unsigned arg = 0;

		TAILQ_FOREACH (p, call->params, link) {
			if (!p->ellipsis)
				fprintf(out, "    %s arg%u;\n",
				        get_ctype_for_param(p),
				        arg++);
		}
	}
	if (param_list_has_variadic(call->params))
		fprintf(out, "    nix_param_t const *ap;\n");
	fprintf(out, "  } %s_%s_args_t;\n", g_gbl_ns, call->name);
}

static void
dump_typedefs(FILE    *out,
              call_t **calls)
{
	size_t n, m;

	fprintf(out, "/* %s System Calls Arguments Data Structures */\n\n", g_gbl_name);

	for (n = m = 0; n < (size_t)g_gbl_limit; n++) {
		if (calls[n] != NULL) {
			if (m++ > 0)
				fprintf(out, "\n");
			dump_def_comment(out, calls[n]);
			dump_typedef(out, calls[n]);
		}
	}
}

static void
dump_cb_decls(FILE    *out,
              call_t **calls)
{
	size_t n, m;

	for (n = m = 0; n < (size_t)g_gbl_limit; n++) {
		if (calls[n] != NULL) {
			param_t *p;
			char    *rettype;
			size_t   arg;

			if (m++ > 0)
				fprintf(out, "\n");

			fprintf(out,
			        "static int\n"
			        "__%s_%s_callback (\n"
			        "  nix_us_syscall_if_t *xus,\n"
			        "  nix_monitor_t *xmon,\n"
			        "  nix_param_t const *_args,\n"
			        "  nix_param_t *_retval)\n",
			        g_gbl_ns,
			        calls[n]->name);
			fprintf(out, "{\n");
			fprintf(out, "  %s_%s_args_t args;\n", g_gbl_ns, calls[n]->name);
			if (calls[n]->rettype != NULL)
				fprintf(out, "  %s result;\n", get_ctype_for_param(calls[n]->rettype));
			fprintf(out, "  int            err;\n");
			if (param_list_has_pointer(calls[n]->params) || (calls[n]->rettype != NULL && calls[n]->rettype->type == NIX_PARAM_POINTER)) {
				fprintf(out, "  nix_memflg_t  mf;\n");
				fprintf(out, "  nix_mem_if_t  *mem = nix_monitor_get_memory (xmon);\n\n");
			} else
				fprintf(out, "\n  (void)xmon;\n");

			if (param_list_count(calls[n]->params) == 0)
				fprintf(out, "  (void)_args;\n\n");

			fprintf(out, "  (void)xus;\n\n");
			fprintf(out, "  _retval->type = %s;\n\n", get_param_type_name(calls[n]->rettype));

			arg = 0;
			if (calls[n]->params != NULL) {

				TAILQ_FOREACH (p, calls[n]->params, link) {
					if (p->ellipsis) {
						fprintf(out, "  args.ap = _args + %zu;\n", arg);
						break;
					}

					fprintf(out, "  args.arg%zu = ", arg);
					if (p->type == NIX_PARAM_POINTER) {
						fprintf(out, "(void *)nix_mem_gtoh (mem, _args[%zu].value.tnosign.u32, &mf);\n", arg); /*XXX u32 is guest-dependant !! */
						fprintf(out, "  if (mf != 0) return %s;\n", g_gbl_bae);
					} else {
						fprintf(out, "_args[%zu].value.tnosign.", arg);
						switch (p->type) /* All this is guest dependant, think of PDP-10 ! */
						{
						case NIX_PARAM_BYTE:
							fprintf(out, "u8");
							break;
						case NIX_PARAM_HALF:
							fprintf(out, "u16");
							break;
						case NIX_PARAM_WORD:
							fprintf(out, "u32");
							break;
						case NIX_PARAM_DWORD:
							fprintf(out, "u64");
							break;
						case NIX_PARAM_INTPTR:
							fprintf(out, "u32");
							break; /*XXX u32 is guest-dependant!! */
						default:
							LCAssert0(0);
						}
						fprintf(out, ";\n");
					}

					arg++;
				}
			}
			fprintf(out, "\n");

			if (calls[n]->rettype != NULL)
				rettype = "&result";
			else
				rettype = "NULL";

			fprintf(out,
			        "  err = %s_%s (xus, xmon, &args, %s);\n",
			        g_gbl_ns,
			        calls[n]->name,
			        rettype);
			fprintf(out, "\n");
			if (calls[n]->rettype != NULL) {
				if (calls[n]->rettype->type == NIX_PARAM_POINTER) {
					fprintf(out, "  _retval->value.tnosign.u64 = nix_mem_htog (mem, (uintptr_t)result, &mf);\n");
					fprintf(out, "  if (mf != 0) return %s;\n", g_gbl_bae);
				} else {
					fprintf(out, "  _retval->value.tnosign.u64 = result;\n");
				}
			}
			fprintf(out, "\n");
			fprintf(out, "  return err;\n");
			fprintf(out, "}\n");
		}
	}
}

static void
dump_sc_table(FILE    *out,
              call_t **calls)
{
	size_t n;

	fprintf(out, "static nix_us_syscall_desc_t const g_%s_us_syscall_descs[] =\n", g_gbl_ns);
	fprintf(out, "  {\n");

	for (n = 0; n < (size_t)g_gbl_limit; n++) {
		fprintf(out, "    ");
		call_emit(out, calls[n]);
		fprintf(out, ",\n");
	}
	fprintf(out, "    ");
	call_emit(out, NULL);
	fprintf(out, "\n");
	fprintf(out, "  };\n");
}

static char *
defize(char const *s)
{
	char *r;

	r = (char *)malloc(strlen(s) + 16);
	if (r != NULL) {
		size_t n;

		sprintf(r, "__%s", s);
		for (n = 0; n < strlen(r); n++) {
			if (!isalnum(r[n]) && r[n] != '_')
				r[n] = '_';
		}
	}
	return r;
}

static void
banner(FILE *out)
{
	fprintf(out,
	        "/* This file is automatically generated from `%s' with %s.\n"
	        " * !!! DO NOT EDIT !!!\n"
	        " */\n\n",
	        g_filename,
	        APPNAME);
}

static void
header_begin(FILE       *out,
             char const *filename)
{
	char *def = defize(filename);

	banner(out);

	fprintf(out, "#ifndef %s\n", def);
	fprintf(out, "#define %s\n\n", def);

	free(def);
}

static void
header_end(FILE       *out,
           char const *filename)
{
	char *def = defize(filename);

	fprintf(out, "\n");
	fprintf(out, "#endif  /* !%s */\n", def);

	free(def);
}

void
produce_syscalls_h(call_t **calls)
{
	FILE *fp;
	char *filename;

	filename = (char *)malloc(strlen(g_gbl_ns) + strlen("-syscalls.h") + 16);
	sprintf(filename, "%s-syscalls.h", g_gbl_ns);

	fprintf(stderr, "==> Producing %s\n", filename);
	fp = fopen(filename, "wt");
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open `%s' for write\n", filename);
		abort();
	}

	header_begin(fp, filename);

	dump_defs(fp, calls);
	fprintf(fp, "\n");

	header_end(fp, filename);

	fclose(fp);
}

void
produce_args_h(call_t **calls)
{
	FILE *fp;
	char *filename;

	filename = (char *)malloc(strlen(g_gbl_ns) + strlen("-args.h") + 16);
	sprintf(filename, "%s-args.h", g_gbl_ns);

	fprintf(stderr, "==> Producing %s\n", filename);
	fp = fopen(filename, "wt");
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open `%s' for write\n", filename);
		abort();
	}

	header_begin(fp, filename);

	dump_typedefs(fp, calls);
	fprintf(fp, "\n");

	header_end(fp, filename);

	fclose(fp);
}

void
produce_callbacks_c(call_t **calls)
{
	FILE *fp;
	char *filename;

	filename = (char *)malloc(strlen(g_gbl_ns) + strlen("-callbacks.c") + 16);
	sprintf(filename, "%s-callbacks.c", g_gbl_ns);

	fprintf(stderr, "==> Producing %s\n", filename);
	fp = fopen(filename, "wt");
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open `%s' for write\n", filename);
		abort();
	}

	banner(fp);

	fprintf(fp, "#include <errno.h>\n\n");
	fprintf(fp, "#include \"nix-syscall.h\"\n");
	fprintf(fp, "#include \"%s-syscalls.h\"\n\n", g_gbl_ns);
	fprintf(fp, "#include \"%s-args.h\"\n\n", g_gbl_ns);

	fprintf(fp, "/* %s System Calls Callback Prototypes */\n\n", g_gbl_name);
	dump_cb_protos(fp, calls);
	fprintf(fp, "\n");

	fprintf(fp, "/* %s System Calls Descriptors */\n\n", g_gbl_name);
	dump_sc_table(fp, calls);

	fprintf(fp, "/* %s System Calls Function Prototypes */\n\n", g_gbl_name);
	dump_func_protos(fp, calls);

	fprintf(fp, "/* %s System Calls Callbacks */\n\n", g_gbl_name);
	dump_cb_decls(fp, calls);

	fprintf(fp, "\n");
	fprintf(fp, "nix_us_syscall_desc_t const *\n");
	fprintf(fp, "%s_us_syscall_get (int scno)\n", g_gbl_ns);
	fprintf(fp, "{ return (scno < 0 || scno >= %s_SYS_last) ? NULL : &g_%s_us_syscall_descs[scno]; }\n",
	        g_gbl_NS, g_gbl_ns);

	fclose(fp);
}

void
produce_skeleton_c(call_t **calls)
{
	FILE *fp;
	char *filename;

	filename = (char *)malloc(strlen(g_gbl_ns) + strlen("-skeleton.c") + 16);
	sprintf(filename, "%s-skeleton.c", g_gbl_ns);

	fprintf(stderr, "==> Producing %s\n", filename);
	fp = fopen(filename, "wt");
	if (fp == NULL) {
		fprintf(stderr, "Error: Cannot open `%s' for write\n", filename);
		abort();
	}

	fprintf(fp,
	        "/* This file is automatically generated from `%s' with %s.\n"
	        " * !!! DO NOT EDIT, COPY THIS FILE INSTEAD !!!\n"
	        " */\n\n",
	        g_filename,
	        APPNAME);

	fprintf(fp, "#include \"nix-syscall.h\"\n");
	fprintf(fp, "#include \"%s-syscalls.h\"\n\n", g_gbl_ns);
	fprintf(fp, "#include \"%s-args.h\"\n\n", g_gbl_ns);

	dump_func_decls(fp, calls);

	fclose(fp);
}

int
main(int argc, char **argv)
{
	call_t      *call;
	call_t     **calls;
	extern FILE *yyin;

	g_calls = call_list_new(NULL);
	if (argc < 2) {
		g_filename = "<stdin>";
		yyin = stdin;
	} else {
		g_filename = strdup(argv[1]);
		yyin = fopen(g_filename, "rt");
		if (yyin == NULL) {
			LCLog(NULL,
			        LCLogError,
			        0,
			        "Cannot open `%s' for reading.",
			        g_filename);
			return EXIT_FAILURE;
		}
	}

	if (yyparse() < 0) {
		LCLog(NULL,
		        LCLogError,
		        0,
		        "An unknown (and unhandled, I'd say) error occurred while parsing data.",
		        0);
		return EXIT_FAILURE;
	}

	fprintf(stderr, "== Parsing succeded.\n");

	if (g_gbl_name == NULL) {
		LCLog(NULL,
		        LCLogError,
		        0,
		        "A `NAME' declaration is required but has not been found.",
		        0);
		return EXIT_FAILURE;
	}
	if (g_gbl_ns == NULL) {
		LCLog(NULL,
		        LCLogError,
		        0,
		        "A `NAMESPACE' declaration is required but has not been found.",
		        0);
		return EXIT_FAILURE;
	}
	if (g_gbl_bae == NULL) {
		LCLog(NULL,
		        LCLogError,
		        0,
		        "A `BAE' (Bad Address Error) declaration is required but has not been found.",
		        0);
		return EXIT_FAILURE;
	}
	if (g_gbl_limit == 0) {
		LCLog(NULL,
		        LCLogError,
		        0,
		        "A `LIMIT' declaration is required but has not been found or its value is zero.",
		        0);
		return EXIT_FAILURE;
	}

	if (g_calls == NULL) {
		LCLog(NULL,
		        LCLogError,
		        0,
		        "No `CALLS' declaration or no calls defined.",
		        0);
		return EXIT_FAILURE;
	}

	printf("       Name = %s\n", g_gbl_name);
	printf("  Namespace = %s\n", g_gbl_ns);
	printf("      Limit = %d\n", g_gbl_limit);

	calls = (call_t **)calloc(g_gbl_limit, sizeof(call_t *));
	if (calls == NULL) {
		LCLog(NULL,
		        LCLogFatal,
		        LCLogErrExit,
		        "Not enough memory to complete the operation.",
		        0);
	}

	TAILQ_FOREACH (call, g_calls, link)
		calls[call->scno] = call;

	g_gbl_NS = uppercase(g_gbl_ns);

	produce_syscalls_h(calls);
	produce_args_h(calls);
	produce_callbacks_c(calls);
	produce_skeleton_c(calls);

	return 0;
}
