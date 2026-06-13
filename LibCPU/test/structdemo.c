/* The demo host library: a single out-parameter function the dispatcher binds and calls.
   Built as a shared library so SymbolReader sees a real export and the binder can dlsym it. */

#include "structdemo.h"

void
demo_stat_fill(struct demo_stat *p)
{
    p->Size = 0x1234;
    p->Mode = 0x0077;
    p->Uid  = 0x0042;
}

void
demo_pack_fill(struct demo_pack *p)
{
    p->A = 0x11;
    p->B = 0x22222222u;     /* truncated to 0x2222 in the 16-bit guest layout */
    p->C = 0x3333;
}
