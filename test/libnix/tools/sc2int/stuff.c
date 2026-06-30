#include "nix-syscall.h"
#include "sc2int.h"

int
yywrap(void)
{
	return 1;
}

void
yyerror(char const *msg)
{
	LCLog(NULL, LCLogFatal, LCLogErrExit, "%s in file `%s' line %u.", msg, g_filename, g_line);
}
