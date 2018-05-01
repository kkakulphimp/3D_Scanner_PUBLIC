// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "MeshSearch.h"

#include "Bbox.h"
#include "Random.h"
#include "Stat.h"
#include "Timer.h"

namespace hh {

void PolygonFaceSpatial::enter(const PolygonFace* ppolyface) {
    const Polygon& opoly = ppolyface->poly;
    assertx(opoly.num()==3);
    Polygon poly = opoly;
    Bbox bbox; poly.get_bbox(bbox);
    auto func_polygonface_in_bbox = [&](const Bbox& bb) -> bool {
        for_int(c, 3) {
            if (bbox[0][c]>bb[1][c] || bbox[1][c]<bb[0][c]) return false;
        }
        int modif = poly.intersect_bbox(bb);
        bool ret = poly.num()>0;
        if (modif) poly = opoly;
        return ret;
    };
    ObjectSpatial::enter(Conv<const PolygonFace*>::e(ppolyface), ppolyface->poly[0], func_polygonface_in_bbox);
}

bool PolygonFaceSpatial::first_along_segment(const Point& p1, const Point& p2,
                                             const PolygonFace*& ret_ppolyface, Point& ret_pint) const {
    Vector vray = p2-p1;
    bool foundint = false;
    float ret_fmin;
    auto func_test_polygonface_with_ray = [&](Univ id) -> bool {
        const PolygonFace* ppolyface = Conv<const PolygonFace*>::d(id);
        const Polygon& poly = ppolyface->poly;
        Point pint;
        if (!poly.intersect_segment(p1, p2, pint)) return false;
        float f = dot(pint-p1, vray);
        if (!foundint || f<ret_fmin) { ret_fmin = f; ret_pint = pint; ret_ppolyface = ppolyface; foundint = true; }
        return true;
    };
    search_segment(p1, p2, func_test_polygonface_with_ray);
    return foundint;
}


MeshSearch::MeshSearch(const GMesh* mesh, bool allow_local_project)
    : _mesh(*assertx(mesh)), _allow_local_project(allow_local_project), _ar_polyface(_mesh.num_faces()) {
    if (getenv_bool("NO_LOCAL_PROJECT")) { Warning("MeshSearch NO_LOCAL_PROJECT"); _allow_local_project = false; }
    int psp_size = int(sqrt(_mesh.num_faces()*.05f));
    if (_allow_local_project) psp_size /= 2;
    psp_size = clamp(10, psp_size, 150);
    HH_TIMER(__meshsearch_build);
    Bbox bbox; bbox.clear();
    for (Vertex v : _mesh.vertices()) { bbox.union_with(_mesh.point(v)); }
    _ftospatial = bbox.get_frame_to_small_cube();
    int fi = 0;
    for (Face f : _mesh.faces()) {
        Polygon poly(3); _mesh.polygon(f, poly); assertx(poly.num()==3);
        for_int(i, 3) { poly[i] *= _ftospatial; }
        _ar_polyface[fi] = PolygonFace(std::move(poly), f);
        fi++;
    }
    _ppsp = make_unique<PolygonFaceSpatial>(psp_size);
    for (PolygonFace& polyface : _ar_polyface) { _ppsp->enter(&polyface); }
    assertx(fi==_mesh.num_faces());
}

Face MeshSearch::search(const Point& p, Face hintf, Bary& bary, Point& clp, float& d2) const {
    Face f = nullptr;
    Polygon poly;
    if (_allow_local_project && hintf) {
        f = hintf;
        int count = 0;
        for (;;) {
            _mesh.polygon(f, poly); assertx(poly.num()==3);
            d2 = project_point_triangle2(p, poly[0], poly[1], poly[2], bary, clp);
            float dfrac = sqrt(d2)*_ftospatial[0][0];
            // if (!count) { HH_SSTAT(Sms_dfrac0, dfrac); }
            if (dfrac>2e-2f) { f = nullptr; break; } // failure
            if (dfrac<1e-6f) break; // success
            Vec3<Vertex> va; _mesh.triangle_vertices(f, va);
            int side = -1;
            for_int(i, 3) {
                if (bary[i]==1.f) { side = i; break; }
            }
            if (side>=0) {
                if (0) {        // slow: randomly choose ccw or clw
                    side = mod3(side+1+(Random::G.unif()<0.5f));
                } else if (0) { // works: always choose ccw
                    side = mod3(side+1);
                } else {        // fastest: jump across vertex
                    Vertex v = va[side];
                    int val = _mesh.degree(v);
                    int nrot = ((val-1)/2)+(Random::G.unif()<0.5f);
                    for_int(i, nrot) {
                        f = _mesh.ccw_face(v, f);
                        if (!f) break; // failure
                    }
                    side = -1;
                }
            } else {
                for_int(i, 3) {
                    if (bary[i]==0.f) { side = i; break; }
                }
                if (side<0) {
                    if (_allow_off_surface) break; // success
                    if (_allow_internal_boundaries) { f = nullptr; break; } // failure
                }
            }
            if (side>=0)
                f = _mesh.opp_face(va[side], f);
            if (!f) {
                if (!_allow_internal_boundaries) assertnever("MeshSearch has hit surface boundary");
                break;          // failure
            }
            if (++count==10) { f = nullptr; break; } // failure
        }
        // HH_SSTAT(Sms_locn, count);
    }
    HH_SSTAT(Sms_loc, !!f);
    if (!f) {
        Point pbb = p*_ftospatial;
        SpatialSearch<PolygonFace*> ss(_ppsp.get(), pbb);
        const PolygonFace& polyface = *assertx(ss.next());
        f = polyface.face;
        _mesh.polygon(f, poly); assertx(poly.num()==3);
        d2 = project_point_triangle2(p, poly[0], poly[1], poly[2], bary, clp);
    }
    return f;
}

} // namespace hh
