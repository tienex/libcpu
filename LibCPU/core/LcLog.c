/*
 * LibCPU logging API implementation (see include/LibCPU/LcLog.h). A severity-gated stderr logger,
 * shared by the LibCPU framework and the libnix host layer.
 */

#include "LibCPU/LcLog.h"

#include <stdio.h>
#include <stdlib.h>

void *
LCLogRegister (char const *tag)
{
    return (void *) tag;            /* the cookie IS the tag string (printed as a prefix) */
}

void
LCLogImpl (void *cookie, int severity, unsigned flags,
           char const *func, char const *file, unsigned line, char const *fmt, ...)
{
    char const *tag = (cookie != NULL) ? (char const *) cookie : "lcx";
    va_list     ap;

    (void) flags;
    (void) file;

    va_start (ap, fmt);
    fprintf (stderr, "%s: ", tag);
    vfprintf (stderr, fmt, ap);
    fputc ('\n', stderr);
    va_end (ap);

    if (severity == LCLogFatal) {
        fprintf (stderr, "%s: fatal in %s (line %u)\n", tag, func, line);
        abort ();
    }
}
