/* C++ member-call fixture. A class with state, a constructor, two add() overloads (int and
   double, which must bind to DIFFERENT symbols), and a const accessor. The invoke demo derives
   a catalog from this header + the compiled library, then actually CALLS each method through
   its bound symbol with a real "this" pointer -- proving the this-as-first-parameter model and
   per-overload symbol selection are ABI-correct, not merely textually plausible. */

namespace ct {
class Counter {
    int n;
public:
    Counter(int start);
    void add(int x);      // n += x
    void add(double x);   // n += (int)(x * 10)  -- distinct behaviour from add(int)
    int get() const;      // returns n (const method: "this" is const-qualified)
};
}
