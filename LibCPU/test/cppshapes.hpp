/* C++ struct/class layout fixture for the header parser. A plain struct, a C++ class (data
   members captured like a struct), and a struct nested in a namespace (reported with its
   qualified name). No #includes, so the parser needs no search paths. */

struct Point { int x; int y; };

class Rect { public: Point origin; int w; int h; };

namespace ui { struct Color { unsigned char r, g, b, a; }; }
