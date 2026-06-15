/* C++ member-function fixture for the header parser and catalog. The parser captures methods
   of a class (qualified with the class name, like ui::Widget::area), gives each non-static
   method an implicit "this" pointer (const-qualified for const methods), leaves static methods
   without one, and keeps the two value() overloads distinct by their parameter types. The
   catalog then matches each prototype to its mangled export. No #includes, so no search paths. */

namespace ui {
class Widget {
public:
    int area() const;          // const method -> "const Widget * this", binds __ZNK2ui6Widget4areaEv
    void resize(int w, int h); // binds __ZN2ui6Widget6resizeEii
    static Widget* create();   // static -> no "this", binds __ZN2ui6Widget6createEv
    int value(int x);          // overload, binds __ZN2ui6Widget5valueEi
    int value(double x);       // overload, binds __ZN2ui6Widget5valueEd
};
}
