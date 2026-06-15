/* C++ symbol-matching fixture for the derivation engine. The companion stub exports the
   mangled symbol __Z6helperd; the catalog must demangle it to "helper(double)" and match
   this prototype by its qualified name. No #includes, so the header parser needs no search
   paths. */

int helper(double d);
