/* Minimal libc header fixture: just enough for the derivation demo. The derivation reads
   these signatures (fputs takes a FILE* the engine auto-fills with stdout; exit takes an
   int) to generate the argument recipes. */

struct _IO_FILE;
typedef struct _IO_FILE FILE;

int  fputs(const char *s, FILE *stream);
void exit(int status);
int  toupper(int c);
