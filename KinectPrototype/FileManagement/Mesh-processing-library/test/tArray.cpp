// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "Array.h"
#include "Vec.h"
#include "RangeOp.h"
#include "ArrayOp.h"
using namespace hh;

int main() {
    struct ST {
        ST(int i)                               : _i(i) { showf("ST(%d)\n", _i); }
        ~ST()                                   { showf("~ST(%d)\n", _i); }
        int _i;
    };
    auto func_make_array = [](int i0, int n) { // -> Array<unique_ptr<ST>>
        Array<unique_ptr<ST>> ar;
        for_int(i, n) { ar.push(make_unique<ST>(i0+i)); }
        return ar;
    };
    {
        SHOW("beg 4");
        Array<unique_ptr<ST>> ar;
        ar.push(make_unique<ST>(4));
        SHOW("end");
    }
    {
        SHOW("beg 4 5");
        Array<unique_ptr<ST>> ar;
        ar.push(make_unique<ST>(4));
        ar.push(make_unique<ST>(5));
        SHOW("end");
    }
    {
        SHOW("beg 4 5 6");
        Array<unique_ptr<ST>> ar;
        ar.push(make_unique<ST>(4));
        ar.push(make_unique<ST>(5));
        ar.push(make_unique<ST>(6));
        for (auto& e : ar) { SHOW(e->_i); }
        SHOW("end");
    }
    {
        SHOW("beg 20");
        Array<unique_ptr<ST>> ar;
        for_int(i, 20) { ar.push(make_unique<ST>(i)); }
        SHOW("end");
    }
    {
        SHOW("beg 100, 2");
        auto ar = func_make_array(100, 2);
        SHOW("end");
    }
    {
        SHOW("beg 500");
        Array<unique_ptr<ST>> ar;
        ar = func_make_array(500, 2);
        SHOW(ar[0]->_i);
        SHOW("beg 600");
        ar = func_make_array(600, 3);
        SHOW("end");
    }
    {
        SHOW("beg 100, 3");
        auto ar = func_make_array(100, 3);
        SHOW("beg 200, 2");
        ar = func_make_array(200, 2);
        SHOW("end");
    }
    {
        SHOW("beg 100, 3");
        auto ar = func_make_array(100, 3);
        SHOW("beg 200, 3");
        ar = func_make_array(200, 3);
        SHOW("end");
    }
    {
        SHOW(CArrayView<int>({1, 2, 3, 4}));
        Array<int> ar { 1, 2, 3, 4 };
        SHOW(sum(ar));
        const Array<int>& ar2 = ar;
        SHOW(sum(ar2));
        SHOW(min(ar2));
        SHOW(sum(ar2));
        SHOW(mean(ar2));
    }
    {
        Array<uchar> ar = { 'a', 'd' };
        SHOW(sum(ar));
        SHOW(min(ar));
        SHOW(mean(ar));
        SHOW(mag2(ar));
        SHOW(mag(ar));
        SHOW(rms(ar));
    }
    {
        int a[5] = {10, 11, 12, 13, 14}; // test C-array
        SHOW(CArrayView<int>(a));
        SHOW(ArView(a));
        CArrayView<int> ar(a);
        SHOW(var(ar));
        SHOW(sqrt(var(ar)));
        SHOW(rms(ar-12));       // rms() and var() have slightly different denominators
        SHOW(reverse(ArView(a)));
        SHOW(ArView(a));
        SHOW(sort(ArView(a)));
        SHOW(ArView(a));
        reverse(a);
        SHOW(ArView(a));
        fill(a, 16);
        SHOW(ArView(a));
    }
    {
        Vec3<int> a(10, 11, 12);
        SHOW(CArrayView<int>(a));
        SHOW(a.view());
        CArrayView<int> ar(a);
        SHOW(var(ar));
        SHOW(sqrt(var(ar)));
        SHOW(rms(ar-12));
    }
    {
        SHOW(sort_unique(V(10, 13, 12, 13, 9, 12, 15, 10).view()));
        SHOW(median(V(10, 13, 12, 13, 9, 12, 15, 10)));
        SHOW(median(V(8, 7, 6, 5, 4, 9, 10)));
        SHOW(median2(V(10, 13, 12, 13, 9, 12, 15, 10)));
        SHOW(mean(median2(V(10, 13, 12, 13, 9, 12, 15, 10))));
        SHOW(median2(V(8, 7, 6, 5, 4, 9, 10)));
        SHOW(mean(median2(V(8, 7, 6, 5, 4, 9, 10))));
    }
    {
        Array<int> ar{8, 7, 6, 5, 4, 9, 10};
        for_int(i, ar.num()) {
            SHOW(i, rank_element(ar, i));
        }
        for (double rankf : {0., .1, .2, .3, .4, .5, .6, .7, .8, .9, 1.}) {
            SHOW(rankf, rankf_element(ar, rankf));
        }
    }
    {
        Array<int> ar1{1, 2};
        SHOW(ar1==V(1, 1+1));
        SHOW(ar1==V(1, 2, 3));
        SHOW(ar1==V(1, 3));
    }
    if (0) {
        // Array<int> ar(5);
        // ArrayView<int> arv(ar); arv = ar;        // is illegal as expected
    }
    if (0) {
        Array<int> ar(2, -1);
        SHOW(ar[2]);            // out-of-bounds error
    }
    {
        using Array3 = Vec<Array<int>, 3>;
        Array3 ar;
        ar[0].push(1);
#if 0
        // Note: gcc unhappy creating copy constructor for Vec<T,n> if T has explicit copy constructor
        // This was my motivation for prior "hh_explicit" macro definition.
        Array3 ar2(ar);
        SHOW(ar2[0]);
#endif
    }
}

namespace hh {
template class Array<unsigned>;
template class Array<double>;
template class Array<const int*>;

template class ArrayView<unsigned>;
template class ArrayView<double>;
template class ArrayView<const int*>;

template class CArrayView<unsigned>;
template class CArrayView<double>;
template class CArrayView<const int*>;

using U = unique_ptr<int>;
template<> Array<U>::Array(int, const U&) { }                               // non-&& definition illegal
template<> Array<U>::Array(const Array<U>&) : ArrayView() { }               // non-&& definition illegal
template<> Array<U>::Array(CArrayView<U>) : ArrayView() { }                 // non-&& definition illegal
template<> Array<U>::Array(std::initializer_list<U>) : ArrayView() { }      // definition illegal
template<> Array<U>& Array<U>::operator=(CArrayView<U>) { return *this; }   // definition illegal
template<> Array<U>& Array<U>::operator=(const Array<U>&) { return *this; } // definition illegal
template<> void Array<U>::init(int, const U&) { }                           // non-&& definition illegal
template<> void Array<U>::push(const U&) { }                                // non-&& definition illegal
template<> void Array<U>::push_array(CArrayView<U>) { }                     // non-&& definition illegal
template<> void Array<U>::unshift(const U&) { }                             // non-&& definition illegal
template<> void Array<U>::unshift(CArrayView<U>) { }                        // non-&& definition illegal
template class Array<U>;
} // namespace hh
