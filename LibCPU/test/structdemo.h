/* Header fixture for struct marshalling. The host struct uses native 32-bit fields
   (offsets 0/4/8, 12 bytes); the derivation engine reads this layout via libclang, and
   the dispatcher converts it to the guest's 16-bit-packed layout (offsets 0/2/4). */

struct demo_stat {
    unsigned int Size;     /* host +0  */
    unsigned int Mode;     /* host +4  */
    unsigned int Uid;      /* host +8  */
};

/* An out-parameter call: fills the struct the caller points at. */
void demo_stat_fill(struct demo_stat *p);

/* A mixed-size struct: the host pads B to a 4-byte boundary (A@0, B@4, C@8 => 12 bytes),
   so the guest's packed 16-bit layout (A@0 size1, B@1 size2, C@3 size2 => 5 bytes) differs
   in both offsets AND field sizes -- exercising de-padding and truncation together. */
struct demo_pack {
    unsigned char  A;      /* host +0 */
    unsigned int   B;      /* host +4 */
    unsigned short C;      /* host +8 */
};

void demo_pack_fill(struct demo_pack *p);
