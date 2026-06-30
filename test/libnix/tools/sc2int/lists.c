#include <stdlib.h>

#include "sc2int.h"

#include "nix-syscall.h"

param_list_t *
param_list_new(param_t *param)
{
	param_list_t *pl;

	pl = (param_list_t *)calloc(1, sizeof(*pl));
	LCAssert0(pl != NULL);
	if (pl != NULL) {
		TAILQ_INIT(pl);
		if (param != NULL)
			pl = param_list_link(pl, param);
	}
	return pl;
}

param_list_t *
param_list_link(param_list_t *list,
                param_t      *param)
{
	LCAssert0(list != NULL);
	LCAssert0(param != NULL);

	TAILQ_INSERT_TAIL(list, param, link);
	return list;
}

param_t *
param_new(nix_param_type_t type)
{
	param_t *p;

	p = (param_t *)calloc(1, sizeof(*p));
	LCAssert0(p != NULL);
	if (p != NULL) {
		p->type = type;
		p->ellipsis = false;
	}
	return p;
}

param_t *
param_new_ellipsis(void)
{
	param_t *p;

	p = (param_t *)calloc(1, sizeof(*p));
	LCAssert0(p != NULL);
	if (p != NULL) {
		p->type = NIX_PARAM_INVALID;
		p->ellipsis = true;
	}
	return p;
}

size_t
param_list_count(param_list_t *pl)
{
	size_t n = 0;

	if (pl != NULL) {
		param_t *p;
		TAILQ_FOREACH(p, pl, link)
		n++;
	}
	return n;
}

call_list_t *
call_list_new(call_t *call)
{
	call_list_t *cl;

	cl = (call_list_t *)calloc(1, sizeof(*cl));
	LCAssert0(cl != NULL);
	if (cl != NULL) {
		TAILQ_INIT(cl);
		if (call != NULL)
			cl = call_list_link(cl, call);
	}
	return cl;
}

call_list_t *
call_list_link(call_list_t *list,
               call_t      *call)
{
	LCAssert0(list != NULL);
	LCAssert0(call != NULL);

	TAILQ_INSERT_TAIL(list, call, link);
	return list;
}

call_t *
call_new(int           scno,
         char         *name,
         param_t      *rettype,
         param_list_t *params)
{
	call_t *c;

	c = (call_t *)calloc(1, sizeof(*c));
	LCAssert0(c != NULL);
	if (c != NULL) {
		c->scno = scno;
		c->name = name;
		c->rettype = rettype;
		c->params = params;
	}
	return c;
}
