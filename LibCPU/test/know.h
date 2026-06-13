/* A small self-contained C header fixture for the knowledge-library header parser.
   No #includes, so libclang parses it without any system search paths, and the
   built-in fallback sees the same declarations. */

typedef unsigned short know_handle;

/* A struct whose layout (field offsets + total size) the parser reports. */
struct know_stat {
    unsigned long  Size;       /* +0  */
    unsigned int   Mode;       /* +8  */
    unsigned short Uid;        /* +12 */
    unsigned short Gid;        /* +14 */
    long long      MTime;      /* +16 */
};

/* Function prototypes: return type, parameters (type + name), and varargs. */
know_handle know_open(const char *path, int flags, unsigned int mode);
long        know_read(know_handle h, void *buf, unsigned long count);
long        know_write(know_handle h, const void *buf, unsigned long count);
int         know_close(know_handle h);
int         know_stat_of(const char *path, struct know_stat *out);
int         know_printf(const char *fmt, ...);
void        know_flush(void);
