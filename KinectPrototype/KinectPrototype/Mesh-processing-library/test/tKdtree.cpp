// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "Kdtree.h"
using namespace hh;

int main() {
    {
        struct cbf {
            Kdtree<int,1>::ECallbackReturn operator()(const int& id, ArrayView<float>, ArrayView<float>,
                                                      Kdtree<int,1>::CBloc) const {
                showf("found index %d\n", id);
                return Kdtree<int,1>::ECallbackReturn::nothing;
            }
        };
        Kdtree<int,1> kd(3);
        using BB = SGrid<float, 2, 1>;
        BB v;
        v = BB{ {0.100f}, {0.200f} }; kd.enter(1, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ {0.700f}, {0.800f} }; kd.enter(2, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ {0.550f}, {0.900f} }; kd.enter(3, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ {0.450f}, {0.520f} }; kd.enter(4, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ {0.251f}, {0.252f} }; kd.enter(5, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ {0.150f}, {0.300f} }; kd.search(v[0], v[1], cbf()); SHOW("");
        v = BB{ {0.400f}, {0.700f} }; kd.search(v[0], v[1], cbf()); SHOW("");
        v = BB{ {0.900f}, {0.910f} }; kd.search(v[0], v[1], cbf()); SHOW("");
    }
    {
        Kdtree<int,2> kd(5);
        using BB = SGrid<float, 2, 2>;
        BB v;
        v = BB{ { 0.100f, 0.510f }, { 0.200f, 0.520f } }; kd.enter(6, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ { 0.626f, 0.100f }, { 0.627f, 0.900f } }; kd.enter(7, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ { 0.100f, 0.626f }, { 0.900f, 0.627f } }; kd.enter(8, v[0], v[1]); kd.print(); SHOW("");
        v = BB{ { 0.400f, 0.326f }, { 0.430f, 0.327f } }; kd.enter(9, v[0], v[1]); kd.print(); SHOW("");
    }
    {
        using U = unique_ptr<int>;
        Kdtree<U,2> kd;
        using BB = SGrid<float, 2, 2>;
        BB v;
        v = BB{ { 0.100f, 0.510f }, { 0.200f, 0.520f } };
        kd.enter(make_unique<int>(), v[0], v[1]);
    }
}

namespace hh {
template class Kdtree<unsigned,1>;
template class Kdtree<float,2>;
template class Kdtree<double,3>;

using U = unique_ptr<int>;
// void Kdtree<U,2>::enter1(const U&, const float*, const float*) { } // definition illegal
template<> Kdtree<U,2>::Entry::Entry(const U&) { }         // definition illegal
template<> void Kdtree<U,2>::rec_print(int, int) const { } // SHOW(U) undefined
template class Kdtree<U,2>;
// full instantiation still tries to instantiate Kdtree::Entry::Entry(const T&), so it fails.
} // namespace hh
