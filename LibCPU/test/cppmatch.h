/* C++ derivation fixture: a namespaced function, an extern "C" function, and a free function.
   The header parser must descend into namespace / extern "C" and produce qualified names, and
   the catalog must demangle the companion stub's exports to match them. No #includes, so the
   parser needs no search paths. (Has C++ markers, so it is parsed as C++.) */

extern "C" int c_init(int n);

namespace gfx { int draw(int x, int y); }

int helper(double d);
