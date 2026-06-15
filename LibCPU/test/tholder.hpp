/* C++ class-template instantiation fixture. The parser captures the template's member shapes
   (with dependent type T) once, then expands the explicit instantiation cn::Holder<int> into a
   concrete catalog entity: a real layout (offsets/sizes from the instantiated type) with T
   substituted to int, and member functions qualified as cn::Holder<int>::value etc. whose
   demangled template-id symbols (__ZN2cn6HolderIiE...) the catalog then matches and binds.

   The instantiation is declared `extern template` here (it is defined in tholder.cpp, which is
   compiled into the library); that is the header idiom for "this template is instantiated in
   the library", and it is what makes the compiler emit concrete symbols to bind to. */

namespace cn {
template <typename T>
class Holder {
    int tag;        // a leading field so the substituted field lands at a non-zero offset
    T item;
public:
    Holder(T v);
    T value() const;       // const method: "this" is const-qualified, symbol carries the K
    void replace(T v);
};
}

extern template class cn::Holder<int>;   // instantiated in the library
