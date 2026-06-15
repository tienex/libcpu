/* Implementation of the C++ member-call fixture (see counter.hpp). Compiled into a shared
   library so the invoke demo can resolve and call the real entry points. The two add()
   overloads behave differently so a wrong overload binding yields a wrong result. */

#include "counter.hpp"

namespace ct {

Counter::Counter (int start) : n (start)
{
}

void
Counter::add (int x)
{
    n += x;
}

void
Counter::add (double x)
{
    n += (int) (x * 10);
}

int
Counter::get () const
{
    return n;
}

}
