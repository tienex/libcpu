/* Header fixture for the catalog-into-dispatch demo: two pure integer-class libc
   functions. The derivation engine reads these signatures; the binder resolves the real
   symbols (present in libSystem/libc) and the dispatcher invokes them for guest syscalls. */

int toupper(int c);
int tolower(int c);
