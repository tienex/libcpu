#ifndef __lists_h
#define __lists_h

#include <sys/queue.h> /* TAILQ_* (was reached via xec-base.h before xec-compat was removed) */

#include "nix-syscall.h"

typedef struct _param {
	TAILQ_ENTRY(_param)
	link;
	bool             ellipsis;
	nix_param_type_t type;
} param_t;

typedef TAILQ_HEAD(_param_list, _param) param_list_t;

typedef struct _call {
	TAILQ_ENTRY(_call)
	link;
	int           scno;
	char         *name;
	param_t      *rettype;
	param_list_t *params;
	char         *since; /* raw "MAJOR.MINOR[.PATCH]" or NULL (= from the start) */
	char         *until; /* raw upper-bound version or NULL (= never removed) */
} call_t;

typedef TAILQ_HEAD(_call_list, _call) call_list_t;

param_list_t *
param_list_new(param_t *param);

param_list_t *
param_list_link(param_list_t *list,
                param_t      *param);

size_t
param_list_count(param_list_t *pl);

param_t *
param_new(nix_param_type_t type);

param_t *
param_new_ellipsis(void);

call_list_t *
call_list_new(call_t *call);

call_list_t *
call_list_link(call_list_t *list,
               call_t      *call);

call_t *
call_new(int           scno,
         char         *name,
         param_t      *rettype,
         param_list_t *params,
         char         *since,
         char         *until);

#endif /* !__lists_h */
