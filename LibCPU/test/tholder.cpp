/* Definitions + explicit instantiation of the template-instantiation fixture (see tholder.hpp).
   The explicit instantiation `template class cn::Holder<int>;` comes AFTER the member
   definitions, so the compiler emits concrete entry points for cn::Holder<int>'s members. */

#include "tholder.hpp"

namespace cn {

template <typename T> Holder<T>::Holder (T v) : tag (1), item (v)
{
}

template <typename T> T Holder<T>::value () const
{
    return item;
}

template <typename T> void Holder<T>::replace (T v)
{
    item = v;
}

}

template class cn::Holder<int>;   // explicit instantiation definition -> emits symbols
