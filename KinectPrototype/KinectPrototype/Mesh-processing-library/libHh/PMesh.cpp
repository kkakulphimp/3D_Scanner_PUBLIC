// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "PMesh.h"

#include <cstring>              // strncmp() etc.

#include "PArray.h"             // ar_pwedge
#include "GMesh.h"              // in extract_gmesh()
#include "Set.h"
#include "HashTuple.h"          // std::hash<std::pair<...>>
#include "BinaryIO.h"           // read_binary_std() and write_binary_std()
#include "RangeOp.h"            // fill()

namespace hh {

constexpr int k_undefined = k_debug ? -INT_MAX : -1;

// *** Performance

// Analysis of memory requirement:
//
//  PMVertex:   3*4     *1v/v   == 12 bytes/vertex
//  PMWedge:    6*4     *1w/v   == 24 bytes/vertex (** Assume: 1 wedge/vertex)
//  PMFace:     4*4     *2f/v   == 32 bytes/vertex
//  PMFaceN:    3*4     *2f/v   == 24 bytes/vertex
//
//  WMesh:      68 bytes/vertex
//
//  AWMesh:     92 bytes/vertex
//
//  PMSVertex:  8*4     *1v/v   == 32 bytes/vertex
//  PMSFace:    4*4     *2f/v   == 32 bytes/vertex
//
//  SMesh:      64 bytes/vertex
//
//  Vsplit:     12+2*12+1*20 *1vsp/v    == 56+ bytes/vertex
//
//   if compacted: 8+2*12+1*20 + fl_matid's + extra wads == 52+ bytes/vertex
//
//  PMesh:      56 bytes/vertex (smaller than WMesh and SMesh!)
//
// optimize:
// - if I had to do it over again, I would require ii==2 always, and remove
//    the vad_small field.
// - place all ar_wad in a separate array.  let PMeshIter keep pointer into it.
// - do the same for fl_matid and fr_matid
//
// Using shorts for flclw and all indices, separate matid array, and no
//  vad_small, I get 38n + 58m bytes for PMesh  (above is 56n + 92m bytes)

// So practically speaking (1999-05-21):
//  D3D mesh : 44n bytes
//  PM  mesh : 40n + 60m bytes

// *** Globals

// k_magic_first_byte works because in network order, the first byte of the
//  integer flclw is the MSB, which should be much smaller than 0xFF.
constexpr int k_magic_first_byte = 0xFF;

// *** Misc

namespace {

inline void attrib_ok(PMWedgeAttrib& a) {
    float len2 = mag2(a.normal);
    // Normal could be all-zero from MeshSimplify,
    //  either because it was zero in original model,
    //  or (less likely) if it became zero in simplification.
    // ABOVE IS NO LONGER TRUE:
    //  Now, MeshSimplify is such that normals are always non-zero.
    // First had the thresh at 1e-4f but numerical imprecision creeped in for large models like the banklamp.
    // const float thresh = 1e-4f;
    const float thresh = 1e-2f;
    if (!assertw(abs(len2)>1e-6f)) return;
    assertx(abs(len2-1.f)<thresh);
}

inline int face_prediction(int fa, int fb, int ii) {
    ASSERTX(fa>=0 || fb>=0);
    return ii==0 ? (fb>=0 ? fb : fa) : (fa>=0 ? fa : fb);
}

} // namespace

// *** Attrib

void interp(PMVertexAttrib& a, const PMVertexAttrib& a1, const PMVertexAttrib& a2, float f1) {
    a.point = interp(a1.point, a2.point, f1);
}

void interp(PMWedgeAttrib& a, const PMWedgeAttrib& a1, const PMWedgeAttrib& a2, float f1) {
    a.normal = ok_normalized(interp(a1.normal, a2.normal, f1));
    a.rgb = interp(a1.rgb, a2.rgb, f1);
    a.uv = interp(a1.uv, a2.uv, f1);
}

void interp(PMSVertexAttrib& a, const PMSVertexAttrib& a1, const PMSVertexAttrib& a2, float f1) {
    interp(a.v, a1.v, a2.v, f1);
    interp(a.w, a1.w, a2.w, f1);
}

void add(PMVertexAttrib& a, const PMVertexAttrib& a1, const PMVertexAttribD& ad) {
    a.point = a1.point+ad.dpoint;
}

void sub(PMVertexAttrib& a, const PMVertexAttrib& a1, const PMVertexAttribD& ad) {
    a.point = a1.point-ad.dpoint;
}

void diff(PMVertexAttribD& ad, const PMVertexAttrib& a1, const PMVertexAttrib& a2) {
    ad.dpoint = a1.point-a2.point;
}

void add(PMWedgeAttrib& a, const PMWedgeAttrib& a1, const PMWedgeAttribD& ad) {
    a.normal = a1.normal+ad.dnormal;
    a.rgb = a1.rgb+ad.drgb;
    a.uv = a1.uv+ad.duv;
}

void add_zero(PMWedgeAttrib& a, const PMWedgeAttribD& ad) {
    a.normal = ad.dnormal;
    a.rgb = ad.drgb;
    a.uv = ad.duv;
}

void sub_noreflect(PMWedgeAttrib& a, const PMWedgeAttrib& abase, const PMWedgeAttribD& ad) {
    a.normal = abase.normal-ad.dnormal;
    a.rgb = abase.rgb-ad.drgb;
    a.uv = abase.uv-ad.duv;
}

void sub_reflect(PMWedgeAttrib& a, const PMWedgeAttrib& abase, const PMWedgeAttribD& ad) {
    // note: may have abase==a -> not really const
    const Vector& n = abase.normal;
    const Vector& d = ad.dnormal;
    // dr == -d +2*(d.n)n
    // an = n + dr
    a.normal = -d+((2.f)*dot(d, n)+1.f)*n;
    a.rgb = abase.rgb-ad.drgb;
    a.uv = abase.uv-ad.duv;
}

void diff(PMWedgeAttribD& ad, const PMWedgeAttrib& a1, const PMWedgeAttrib& a2) {
    ad.dnormal = a1.normal-a2.normal;
    float a1rgb0 = a1.rgb[0], a1rgb1 = a1.rgb[1], a1rgb2 = a1.rgb[2];
    float a2rgb0 = a2.rgb[0], a2rgb1 = a2.rgb[1], a2rgb2 = a2.rgb[2];
    // Handle BIGFLOAT for use in Filterprog.
    if (a1rgb0==BIGFLOAT) a1rgb0 = 0.f;
    if (a1rgb1==BIGFLOAT) a1rgb1 = 0.f;
    if (a1rgb2==BIGFLOAT) a1rgb2 = 0.f;
    if (a2rgb0==BIGFLOAT) a2rgb0 = 0.f;
    if (a2rgb1==BIGFLOAT) a2rgb1 = 0.f;
    if (a2rgb2==BIGFLOAT) a2rgb2 = 0.f;
    ad.drgb[0] = a1rgb0-a2rgb0;
    ad.drgb[1] = a1rgb1-a2rgb1;
    ad.drgb[2] = a1rgb2-a2rgb2;
    float a1uv0 = a1.uv[0], a1uv1 = a1.uv[1];
    float a2uv0 = a2.uv[0], a2uv1 = a2.uv[1];
    if (a1uv0==BIGFLOAT) a1uv0 = 0.f;
    if (a1uv1==BIGFLOAT) a1uv1 = 0.f;
    if (a2uv0==BIGFLOAT) a2uv0 = 0.f;
    if (a2uv1==BIGFLOAT) a2uv1 = 0.f;
    ad.duv[0] = a1uv0-a2uv0;
    ad.duv[1] = a1uv1-a2uv1;
}

int compare(const PMVertexAttrib& a1, const PMVertexAttrib& a2) {
    return compare(a1.point, a2.point);
}

int compare(const PMVertexAttrib& a1, const PMVertexAttrib& a2, float tol) {
    return compare(a1.point, a2.point, tol);
}

int compare(const PMWedgeAttrib& a1, const PMWedgeAttrib& a2) {
    int r;
    r = compare(a1.normal, a2.normal); if (r) return r;
    r = compare(a1.rgb, a2.rgb); if (r) return r;
    r = compare(a1.uv, a2.uv); return r;
}

int compare(const PMWedgeAttrib& a1, const PMWedgeAttrib& a2, float tol) {
    int r;
    r = compare(a1.normal, a2.normal, tol); if (r) return r;
    r = compare(a1.rgb, a2.rgb, tol); if (r) return r;
    r = compare(a1.uv, a2.uv, tol); return r;
}

// *** WMesh

void WMesh::read(std::istream& is, const PMeshInfo& pminfo) {
    assertx(!_vertices.num());
    _materials.read(is);
    int nvertices, nwedges, nfaces; {
        string sline; assertx(my_getline(is, sline));
        assertx(sscanf(sline.c_str(), "nvertices=%d nwedges=%d nfaces=%d", &nvertices, &nwedges, &nfaces)==3);
    }
    _vertices.init(nvertices);
    _wedges.init(nwedges);
    _faces.init(nfaces);
    for_int(v, nvertices) {
        assertx(read_binary_std(is, _vertices[v].attrib.point.view()));
    }
    // PM base_mesh has property that
    //  _wedges[w].vertex==w for w<_vertices.num().
    // This could be exploited. optimize.
    const int nrgb = pminfo._read_version < 2 ? 2 : pminfo._has_rgb*3 + pminfo._has_uv*2;
    Array<float> buf(3+nrgb);
    for_int(w, nwedges) {
        assertx(read_binary_std(is, ArView(_wedges[w].vertex))); assertx(_vertices.ok(_wedges[w].vertex));
        assertx(read_binary_std(is, buf));
        Vector& nor = _wedges[w].attrib.normal;
        A3dColor& rgb = _wedges[w].attrib.rgb;
        UV& uv = _wedges[w].attrib.uv;
        const float* p = buf.data();
        if (1) {
            for_int(c, 3) { nor[c] = *p++; }
        }
        if (pminfo._has_rgb) {
            for_int(c, 3) { rgb[c] = *p++; }
        } else {
            fill(rgb, 0.f);
        }
        if (pminfo._has_uv) {
            for_int(c, 2) { uv[c] = *p++; }
        } else {
            fill(uv, 0.f);
            if (pminfo._read_version<2) p += 2;
        }
        ASSERTX(p==buf.end());
    }
    for_int(f, nfaces) {
        assertx(read_binary_std(is, _faces[f].wedges.view()));
        ushort lmatid; assertx(read_binary_std(is, ArView(lmatid)));
        int& matid = _faces[f].attrib.matid;
        matid = lmatid;
        assertx(_materials.ok(matid));
    }
}

void WMesh::write(std::ostream& os, const PMeshInfo& pminfo) const {
    _materials.write(os);
    os << "nvertices=" << _vertices.num() << " nwedges=" << _wedges.num() << " nfaces=" << _faces.num() << '\n';
    for_int(v, _vertices.num()) {
        write_binary_std(os, _vertices[v].attrib.point.view());
    }
    for_int(w, _wedges.num()) {
        write_binary_std(os, ArView(_wedges[w].vertex));
        if (1) write_binary_std(os, _wedges[w].attrib.normal.view());
        if (pminfo._has_rgb) write_binary_std(os, _wedges[w].attrib.rgb.view());
        if (pminfo._has_uv) write_binary_std(os, _wedges[w].attrib.uv.view());
    }
    for_int(f, _faces.num()) {
        write_binary_std(os, _faces[f].wedges.view());
        int matid = _faces[f].attrib.matid&~AWMesh::k_Face_visited_mask;
        ushort lmatid = narrow_cast<ushort>(matid);
        write_binary_std(os, ArView(lmatid));
    }
    assertx(os);
}

void WMesh::extract_gmesh(GMesh& gmesh, const PMeshInfo& pminfo) const {
    const int no_ref = -1, multiple_refs = -2;
    Array<int> wedgeref(_vertices.num(), no_ref);
    for_int(w, _wedges.num()) {
        int v = _wedges[w].vertex;
        int& wr = wedgeref[v];
        wr = wr==no_ref ? w : multiple_refs;
    }
    assertx(!gmesh.num_vertices());
    string str;
    for_int(v, _vertices.num()) {
        Vertex gv = gmesh.create_vertex();
        ASSERTX(gmesh.vertex_id(gv)==v+1);
        gmesh.set_point(gv, _vertices[v].attrib.point);
        int wr = wedgeref[v];
        assertx(wr!=no_ref);
        if (wr!=multiple_refs) {
            gmesh.update_string(gv, "wid", csform(str, "%d", wr+1));
            const Vector& nor = _wedges[wr].attrib.normal;
            gmesh.update_string(gv, "normal", csform_vec(str, nor));
            const A3dColor& rgb = _wedges[wr].attrib.rgb;
            if (pminfo._has_rgb) {
                gmesh.update_string(gv, "rgb", csform_vec(str, rgb));
            }
            const UV& uv = _wedges[wr].attrib.uv;
            if (pminfo._has_uv) {
                gmesh.update_string(gv, "uv", csform_vec(str, uv));
            }
        }
    }
    Array<Vertex> gva;
    for_int(f, _faces.num()) {
        gva.init(0);
        for_int(j, 3) {
            int w = _faces[f].wedges[j];
            int v = _wedges[w].vertex;
            gva.push(gmesh.id_vertex(v+1));
        }
        Face gf = gmesh.create_face(gva);
        int matid = _faces[f].attrib.matid&~AWMesh::k_Face_visited_mask;
        gmesh.set_string(gf, _materials.get(matid).c_str());
        for_int(j, 3) {
            int w = _faces[f].wedges[j];
            int v = _wedges[w].vertex;
            if (wedgeref[v]!=multiple_refs) continue;
            Corner gc = gmesh.corner(gva[j], gf);
            gmesh.update_string(gc, "wid", csform(str, "%d", w+1));
            const Vector& nor = _wedges[w].attrib.normal;
            gmesh.update_string(gc, "normal", csform_vec(str, nor));
            const A3dColor& rgb = _wedges[w].attrib.rgb;
            if (pminfo._has_rgb) {
                gmesh.update_string(gc, "rgb", csform_vec(str, rgb));
            }
            const UV& uv = _wedges[w].attrib.uv;
            if (pminfo._has_uv) {
                gmesh.update_string(gc, "uv", csform_vec(str, uv));
            }
        }
    }
}

void WMesh::ok() const {
    for_int(w, _wedges.num()) {
        int v = _wedges[w].vertex;
        assertx(_vertices.ok(v));
    }
    for_int(f, _faces.num()) {
        Set<int> setw;
        for_int(j, 3) {
            int w = _faces[f].wedges[j];
            assertx(_wedges.ok(w));
            assertx(setw.add(w));
        }
        int matid = _faces[f].attrib.matid&~AWMesh::k_Face_visited_mask;
        assertx(_materials.ok(matid));
    }
}

// *** Vsplit

void Vsplit::read(std::istream& is, const PMeshInfo& pminfo) {
    assertx(read_binary_std(is, ArView(flclw)));
    assertx(read_binary_std(is, ArView(vlr_offset1)));
    assertx(read_binary_std(is, ArView(code)));
    if (code & (FLN_MASK | FRN_MASK)) {
        assertx(read_binary_std(is, ArView(fl_matid)));
        assertx(read_binary_std(is, ArView(fr_matid)));
    } else {
        fl_matid = 0; fr_matid = 0;
    }
    const int max_nwa = 6;
    int nwa = expected_wad_num(pminfo); ASSERTX(nwa<=max_nwa);
    const int nrgb = pminfo._has_rgb*3 + pminfo._has_uv*2;
    int wadlength = 3+nrgb;
    Vec<float, 6+max_nwa*(3+3+2)+2> buf;
    const int bufn = 6+nwa*wadlength+2*pminfo._has_resid;
    assertx(bufn<=buf.num());
    assertx(read_binary_std(is, buf.head(bufn)));
    Vector& vlarge = vad_large.dpoint; for_int(c, 3) { vlarge[c] = buf[0+c]; }
    Vector& vsmall = vad_small.dpoint; for_int(c, 3) { vsmall[c] = buf[3+c]; }
    ar_wad.init(nwa);
    for_int(i, nwa) {
        int bufw = 6+i*wadlength;
        Vector& nor = ar_wad[i].dnormal;
        A3dColor& rgb = ar_wad[i].drgb;
        UV& uv = ar_wad[i].duv;
        float* p = &buf[bufw];
        if (1) {
            for_int(c, 3) { nor[c] = *p++; }
        }
        if (pminfo._has_rgb) {
            for_int(c, 3) { rgb[c] = *p++; }
        } else {
            fill(rgb, 0.f);
        }
        if (pminfo._has_uv) {
            for_int(c, 2) { uv[c] = *p++; }
        } else {
            fill(uv, 0.f);
        }
    }
    if (pminfo._has_resid) {
        float* p = &buf[6+nwa*wadlength+0];
        resid_uni = *p++;
        resid_dir = *p++;
    } else {
        resid_uni = 0.f; resid_dir = 0.f;
    }
}

void Vsplit::write(std::ostream& os, const PMeshInfo& pminfo) const {
    write_binary_std(os, ArView(flclw));
    write_binary_std(os, ArView(vlr_offset1));
    write_binary_std(os, ArView(code));
    if (code & (FLN_MASK | FRN_MASK)) {
        write_binary_std(os, ArView(fl_matid));
        write_binary_std(os, ArView(fr_matid));
    }
    write_binary_std(os, vad_large.dpoint.view());
    write_binary_std(os, vad_small.dpoint.view());
    for (const PMWedgeAttribD& wad : ar_wad) {
        if (1) write_binary_std(os, wad.dnormal.view());
        if (pminfo._has_rgb) write_binary_std(os, wad.drgb.view());
        if (pminfo._has_uv) write_binary_std(os, wad.duv.view());
    }
    if (pminfo._has_resid) {
        write_binary_std(os, ArView(resid_uni));
        write_binary_std(os, ArView(resid_dir));
    }
}

void Vsplit::ok() const {
    // assertx(ar_wad.num()==expected_wad_num(now_missing_pminfo));
}

int Vsplit::expected_wad_num(const PMeshInfo& pminfo) const {
    if (pminfo._has_wad2) return 2;
    // optimize: construct static const lookup table on (S_MASK | T_MASK).
    int nwa = 0;
    if (1) {
        bool nt = !(code&T_LSAME);
        bool ns = !(code&S_LSAME);
        nwa += nt && ns ? 2 : 1;
    }
    if (vlr_offset1>1) {
        bool nt = !(code&T_RSAME);
        bool ns = !(code&S_RSAME);
        if (nt && ns) {
            if (!(code&T_CSAME)) nwa++;
            if (!(code&S_CSAME)) nwa++;
        } else {
            int ii = (code&II_MASK)>>II_SHIFT;
            switch (ii) {
             bcase 2:
                if (!(code&T_CSAME)) nwa++;
             bcase 0:
                if (!(code&S_CSAME)) nwa++;
             bcase 1:
                if (!(code&T_CSAME) || !(code&S_CSAME)) nwa++;
             bdefault: assertnever("");
            }
        }
    }
    if (code&L_NEW) nwa++;
    if (code&R_NEW) nwa++;
    return nwa;
}

// *** AWMesh

int AWMesh::most_clw_face(int v, int f) {
    int ff = f, lastf;
    do { lastf = ff; ff = _fnei[ff].faces[mod3(get_jvf(v, ff)+2)]; } while (ff!=k_undefined && ff!=f);
    return (ff==k_undefined) ? lastf : k_undefined;
}

int AWMesh::most_ccw_face(int v, int f) {
    int ff = f, lastf;
    do { lastf = ff; ff = _fnei[ff].faces[mod3(get_jvf(v, ff)+1)]; } while (ff!=k_undefined && ff!=f);
    return (ff==k_undefined) ? lastf : k_undefined;
}

bool AWMesh::is_boundary(int v, int f) {
    return most_ccw_face(v, f)!=k_undefined;
}

bool AWMesh::gather_faces(int v, int f, Array<int>& faces) {
    int ff = f; faces.init(0);
    do {
        faces.push(ff); ff = _fnei[ff].faces[mod3(get_jvf(v, ff)+1)];
    } while (ff!=k_undefined && ff!=f);
    if (ff==f) return false;
    ff = _fnei[f].faces[mod3(get_jvf(v, f)+2)];
    while (ff!=k_undefined) {
        faces.push(ff); ff = _fnei[ff].faces[mod3(get_jvf(v, ff)+2)];
    }
    return true;
}

void AWMesh::rotate_ccw(int v, int& w, int& f) const {
    f = _fnei[f].faces[mod3(get_jvf(v, f)+1)];
    w = f==k_undefined ? k_undefined : get_wvf(v, f);
}

void AWMesh::rotate_clw(int v, int& w, int& f) const {
    f = _fnei[f].faces[mod3(get_jvf(v, f)+2)];
    w = f==k_undefined ? k_undefined : get_wvf(v, f);
}

void AWMesh::read(std::istream& is, const PMeshInfo& pminfo) {
    WMesh::read(is, pminfo);
    construct_adjacency();
}

void AWMesh::write(std::ostream& os, const PMeshInfo& pminfo) const {
    WMesh::write(os, pminfo);
}

void AWMesh::apply_vsplit(const Vsplit& vspl, const PMeshInfo& pminfo, Ancestry* ancestry) {
    // SHOW("**vsplit");
    // Sanity checks
    ASSERTX(_faces.ok(vspl.flclw));
    const bool isl = true; const bool isr = vspl.vlr_offset1>1;
    // Allocate space for new faces now, since ar_pwedges points into _faces array.
    _faces.add(isr ? 2 : 1), _fnei.add(isr ? 2 : 1); // !remember _fnei
    // Get vertices, faces, and wedges in neigbhorhood.
    int vs;
    unsigned code = vspl.code;
    int ii = (code&Vsplit::II_MASK)>>Vsplit::II_SHIFT;
    int vs_index = (code&Vsplit::VSINDEX_MASK)>>Vsplit::VSINDEX_SHIFT;
    int flccw, flclw;               // either (not both) may be k_undefined
    int frccw, frclw;               // either (or both) may be k_undefined
    int wlccw, wlclw, wrccw, wrclw; // ==k_undefined if faces do not exist
    int jlccw, jlclw, jrccw, jrclw; // only defined if faces exist
    dummy_init(jlccw, jlclw, jrccw, jrclw);
    if (k_debug) jlccw = jlclw = jrccw = jrclw = INT_MAX;
    PArray<int*,10> ar_pwedges;
    if (vspl.vlr_offset1==0) {
        // Extremely rare case when flclw does not exist.
        flclw = k_undefined;
        wlclw = k_undefined;
        flccw = vspl.flclw;
        jlccw = vs_index;
        wlccw = _faces[flccw].wedges[jlccw];
        vs = _wedges[wlccw].vertex;
        frccw = k_undefined; frclw = k_undefined; wrccw = k_undefined; wrclw = k_undefined;
    } else {
        flclw = vspl.flclw;
        jlclw = vs_index;
        int* pwlclw = &_faces[flclw].wedges[jlclw];
        wlclw = *pwlclw;
        vs = _wedges[wlclw].vertex;
        flccw = _fnei[flclw].faces[mod3(jlclw+1)];
        if (flccw==k_undefined) {
            wlccw = k_undefined;
        } else {
            jlccw = get_jvf(vs, flccw);
            wlccw = _faces[flccw].wedges[jlccw];
        }
        if (!isr) {
            frccw = k_undefined; frclw = k_undefined; wrccw = k_undefined; wrclw = k_undefined;
            ar_pwedges.push(pwlclw);
            // Rotate around and record all wedges CLW from wlclw.
            int j0 = jlclw;
            int f = flclw;
            for (;;) {
                f = _fnei[f].faces[mod3(j0+2)];
                if (f<0) break;
                ASSERTX(f!=flclw && f!=flccw);
                j0 = get_jvf(vs, f);
                ar_pwedges.push(&_faces[f].wedges[j0]);
            }
        } else {
            ar_pwedges.init(vspl.vlr_offset1-1);
            ar_pwedges[0] = pwlclw;
            // Rotate around the first x-1 faces.
            int j0 = jlclw;
            int f = flclw;
            for_int(count, vspl.vlr_offset1-2) {
                f = _fnei[f].faces[mod3(j0+2)];
                ASSERTX(f>=0 && f!=flclw && f!=flccw);
                j0 = get_jvf(vs, f);
                ar_pwedges[count+1] = &_faces[f].wedges[j0];
            }
            frccw = f;
            // On the last face, find adjacent faces.
            jrccw = j0;
            wrccw = _faces[frccw].wedges[jrccw];
            frclw = _fnei[frccw].faces[mod3(j0+2)];
            if (frclw==k_undefined) {
                wrclw = k_undefined;
            } else {
                jrclw = get_jvf(vs, frclw);
                wrclw = _faces[frclw].wedges[jrclw];
            }
        }
    }
    ASSERTX(flccw<0 || jlccw==get_jvf(vs, flccw));
    ASSERTX(flclw<0 || jlclw==get_jvf(vs, flclw));
    ASSERTX(frccw<0 || jrccw==get_jvf(vs, frccw));
    ASSERTX(frclw<0 || jrclw==get_jvf(vs, frclw));
    // Add a new vertex.
    int vt = _vertices.add(1);
    // Check equivalence of wedges across (vs, vl) and (vs, vr)
#if defined(HH_DEBUG)
    {
        bool thru_l = isl && (code&Vsplit::S_LSAME) && (code&Vsplit::T_LSAME);
        bool thru_r = isr && (code&Vsplit::S_RSAME) && (code&Vsplit::T_RSAME);
        bool both_f_l = isl && flccw>=0 && flclw>=0;
        bool both_f_r = isr && frccw>=0 && frclw>=0;
        bool all_same = both_f_l && both_f_r && wlccw==wlclw && wrccw==wrclw && wlccw==wrccw;
        if (both_f_l)
            assertx((wlccw==wlclw)==thru_l || (!thru_l && all_same && thru_r));
        if (both_f_r)
            assertx((wrccw==wrclw)==thru_r || (!thru_r && all_same && thru_l));
        // This indicates constraints between S_MASK, T_MASK, and
        //  equivalence of corners in M^i.
        // Could predict S_MASK from ii,
        //  but in practice Huffman coding of
        //   (II_MASK | S_MASK | T_MASK | L_MASK | R_MASK) symbol
        //  should take care of that.
        if (!isr) {
            assertx((code&Vsplit::S_RSAME) && (code&Vsplit::T_RSAME));
            assertx(!(code&Vsplit::S_CSAME) && !(code&Vsplit::T_CSAME));
        }
    }
#endif
    // Save current number of wedges if ancestry.
    int onumwedges; dummy_init(onumwedges); // defined if ancestry
    if (k_debug) onumwedges = INT_MAX;
    if (ancestry) onumwedges = _wedges.num();
    // First un-share wedges around vt (may be gap on top).  May modify wlclw and wrccw!
    int wnl = k_undefined, wnr = k_undefined;
    int iil = 0, iir = ar_pwedges.num()-1;
    if (isl && wlclw==wlccw) {  // first go clw.
        if (1) {
            wnl = _wedges.add(1); _wedges[wnl].vertex = vt;
            _wedges[wnl].attrib = _wedges[wlccw].attrib;
        }
        wlclw = wnl;            // has been changed
        ASSERTX(*ar_pwedges[iil]==wlccw);
        for (;;) {
            *ar_pwedges[iil] = wnl;
            iil++;
            if (iil>iir) {
                wrccw = wnl;    // has been changed
                break;
            }
            if (*ar_pwedges[iil]!=wlccw) break;
        }
    }
    if (isr && wrccw==wrclw) {  // now go ccw from other side.
        if (wrclw==wlccw && wnl>=0) {
            wnr = wnl;
        } else {
            wnr = _wedges.add(1); _wedges[wnr].vertex = vt;
            _wedges[wnr].attrib = _wedges[wrclw].attrib;
        }
        wrccw = wnr;            // has been changed
        ASSERTX(*ar_pwedges[iir]==wrclw);
        for (;;) {
            *ar_pwedges[iir] = wnr;
            --iir;
            if (iir<iil) {
                if (iir<0) wlclw = wnr; // has been changed
                break;
            }
            if (*ar_pwedges[iir]!=wrclw) break;
        }
    }
    // Add other new wedges and record wedge ancestries
    int wvtfl, wvtfr;
    if (!isr) {
        wvtfr = k_undefined;
        switch (code&Vsplit::T_MASK) {
         bcase Vsplit::T_LSAME | Vsplit::T_RSAME:
            wvtfl = wlclw;
         bcase Vsplit::T_RSAME:
            wvtfl = _wedges.add(1); _wedges[wvtfl].vertex = vt;
         bdefault: assertnever("");
        }
        ASSERTX(wvtfl>=0);
    } else {
        switch (code&Vsplit::T_MASK) {
         bcase Vsplit::T_LSAME | Vsplit::T_RSAME | Vsplit::T_CSAME:
            wvtfl = wlclw;
            wvtfr = wrccw;
            ASSERTX(wvtfl==wvtfr);
         bcase Vsplit::T_LSAME | Vsplit::T_RSAME:
            wvtfl = wlclw;
            wvtfr = wrccw;
            ASSERTX(wvtfl!=wvtfr);
         bcase Vsplit::T_LSAME | Vsplit::T_CSAME:
            wvtfl = wlclw;
            wvtfr = wvtfl;
         bcase Vsplit::T_RSAME | Vsplit::T_CSAME:
            wvtfl = wrccw;
            wvtfr = wvtfl;
         bcase Vsplit::T_LSAME:
            wvtfl = wlclw;
            wvtfr = _wedges.add(1); _wedges[wvtfr].vertex = vt;
         bcase Vsplit::T_RSAME:
            wvtfl = _wedges.add(1); _wedges[wvtfl].vertex = vt;
            wvtfr = wrccw;
         bcase Vsplit::T_CSAME:
            wvtfl = _wedges.add(1); _wedges[wvtfl].vertex = vt;
            wvtfr = wvtfl;
         bcase 0:
            wvtfl = _wedges.add(1); _wedges[wvtfl].vertex = vt;
            wvtfr = _wedges.add(1); _wedges[wvtfr].vertex = vt;
         bdefault: assertnever("");
        }
        ASSERTX(wvtfl>=0 && wvtfr>=0);
    }
    int wvsfl, wvsfr;
    if (!isr) {
        wvsfr = k_undefined;
        switch (code&Vsplit::S_MASK) {
         bcase Vsplit::S_LSAME | Vsplit::S_RSAME:
            wvsfl = wlccw;
         bcase Vsplit::S_RSAME:
            wvsfl = _wedges.add(1); _wedges[wvsfl].vertex = vs;
         bdefault: assertnever("");
        }
        ASSERTX(wvsfl>=0);
    } else {
        switch (code&Vsplit::S_MASK) {
         bcase Vsplit::S_LSAME | Vsplit::S_RSAME | Vsplit::S_CSAME:
            wvsfl = wlccw;
            wvsfr = wrclw;
            ASSERTX(wvsfl==wvsfr);
         bcase Vsplit::S_LSAME | Vsplit::S_RSAME:
            wvsfl = wlccw;
            wvsfr = wrclw;
            ASSERTX(wvsfl!=wvsfr);
         bcase Vsplit::S_LSAME | Vsplit::S_CSAME:
            wvsfl = wlccw;
            wvsfr = wvsfl;
         bcase Vsplit::S_RSAME | Vsplit::S_CSAME:
            wvsfl = wrclw;
            wvsfr = wvsfl;
         bcase Vsplit::S_LSAME:
            wvsfl = wlccw;
            wvsfr = _wedges.add(1); _wedges[wvsfr].vertex = vs;
         bcase Vsplit::S_RSAME:
            wvsfl = _wedges.add(1); _wedges[wvsfl].vertex = vs;
            wvsfr = wrclw;
         bcase Vsplit::S_CSAME:
            wvsfl = _wedges.add(1); _wedges[wvsfl].vertex = vs;
            wvsfr = wvsfl;
         bcase 0:
            wvsfl = _wedges.add(1); _wedges[wvsfl].vertex = vs;
            wvsfr = _wedges.add(1); _wedges[wvsfr].vertex = vs;
         bdefault: assertnever("");
        }
        ASSERTX(wvsfl>=0 && wvsfr>=0);
    }
    int wvlfl, wvrfr;
    if (isl) {
        switch (code&Vsplit::L_MASK) {
         bcase Vsplit::L_ABOVE:
            wvlfl = _faces[flclw].wedges[mod3(jlclw+2)];
         bcase Vsplit::L_BELOW:
            wvlfl = _faces[flccw].wedges[mod3(jlccw+1)];
         bcase Vsplit::L_NEW:
         {
             wvlfl = _wedges.add(1);
             int vl = _wedges[(flclw>=0
                               ? _faces[flclw].wedges[mod3(jlclw+2)]
                               : _faces[flccw].wedges[mod3(jlccw+1)])].vertex;
             _wedges[wvlfl].vertex = vl;
         }
         bdefault: assertnever("");
        }
    }
    if (!isr) {
        wvrfr = k_undefined;
    } else {
        switch (code&Vsplit::R_MASK) {
         bcase Vsplit::R_ABOVE:
            wvrfr = _faces[frccw].wedges[mod3(jrccw+1)];
         bcase Vsplit::R_BELOW:
            wvrfr = _faces[frclw].wedges[mod3(jrclw+2)];
         bcase Vsplit::R_NEW:
         {
             wvrfr = _wedges.add(1);
             int vr = _wedges[_faces[frccw].wedges[mod3(jrccw+1)]].vertex;
             _wedges[wvrfr].vertex = vr;
         }
         bdefault: assertnever("");
        }
    }
    // Add 1 or 2 faces, and update adjacency information.
    int fl, fr;
    if (isr) {
        fr = _faces.num()-1; fl = fr-1;
    } else {
        fr = k_undefined; fl = _faces.num()-1;
    }
    if (isl) {
        _faces[fl].wedges[0] = wvsfl;
        _faces[fl].wedges[1] = wvtfl;
        _faces[fl].wedges[2] = wvlfl;
        if (flccw>=0) _fnei[flccw].faces[mod3(jlccw+2)] = fl;
        if (flclw>=0) _fnei[flclw].faces[mod3(jlclw+1)] = fl;
        // could use L_MASK instead of ii for prediction instead.
        _faces[fl].attrib.matid = (((code&Vsplit::FLN_MASK) ? vspl.fl_matid :
                                    _faces[face_prediction(flclw, flccw, ii)].attrib.matid)
                                   |_cur_frame_mask);
        ASSERTX(_materials.ok(_faces[fl].attrib.matid&~k_Face_visited_mask));
        _fnei[fl].faces[0] = flclw;
        _fnei[fl].faces[1] = flccw;
        _fnei[fl].faces[2] = fr;
    }
    if (isr) {
        _faces[fr].wedges[0] = wvsfr;
        _faces[fr].wedges[1] = wvrfr;
        _faces[fr].wedges[2] = wvtfr;
        if (frccw>=0) _fnei[frccw].faces[mod3(jrccw+2)] = fr;
        if (frclw>=0) _fnei[frclw].faces[mod3(jrclw+1)] = fr;
        _faces[fr].attrib.matid = (((code&Vsplit::FRN_MASK) ? vspl.fr_matid :
                                    _faces[face_prediction(frccw, frclw, ii)].attrib.matid)
                                   |_cur_frame_mask);
        ASSERTX(_materials.ok(_faces[fr].attrib.matid&~k_Face_visited_mask));
        _fnei[fr].faces[0] = frccw;
        _fnei[fr].faces[1] = fl;
        _fnei[fr].faces[2] = frclw;
    }
    // Update wedge vertices.
    for (; iil<=iir; iil++) {
        int w = *ar_pwedges[iil];
        ASSERTX(_wedges.ok(w));
        _wedges[w].vertex = vt;
    }
    ASSERTX(!isl || _wedges[_faces[fl].wedges[0]].vertex==vs);
    ASSERTX(!isr || _wedges[_faces[fr].wedges[0]].vertex==vs);
    ASSERTX(!isl || _wedges[_faces[fl].wedges[1]].vertex==vt);
    ASSERTX(!isr || _wedges[_faces[fr].wedges[2]].vertex==vt);
    // Update vertex attributes.
    {
        PMVertexAttrib& vas = _vertices[vs].attrib;
        PMVertexAttrib& vat = _vertices[vt].attrib;
        switch (ii) {
         bcase 2:
            add(vat, vas, vspl.vad_large);
            add(vas, vas, vspl.vad_small);
         bcase 0:
            add(vat, vas, vspl.vad_small);
            add(vas, vas, vspl.vad_large);
         bcase 1:
         {
             PMVertexAttrib vam;
             add(vam, vas, vspl.vad_small);
             add(vat, vam, vspl.vad_large);
             sub(vas, vam, vspl.vad_large);
         }
         bdefault: assertnever("");
        }
    }
    // Update wedge attributes.
    PMWedgeAttrib awvtfr, awvsfr; dummy_init(awvtfr, awvsfr);
    int lnum = 0;
    if (pminfo._has_wad2) {
        assertx(vspl.ar_wad.num()==2);
        int ns = !(code&Vsplit::S_LSAME);
        if (ns) _wedges[wvsfl].attrib = _wedges[wvtfl].attrib;
        add(_wedges[wvtfl].attrib, _wedges[wvsfl].attrib, vspl.ar_wad[0]);
        add(_wedges[wvsfl].attrib, _wedges[wvsfl].attrib, vspl.ar_wad[1]);
        assertx(!ancestry);
        goto GOTO_VSPLIT_WAD2;
    }
    if (isr) {
        awvtfr = _wedges[wvtfr].attrib; // backup for isr
        awvsfr = _wedges[wvsfr].attrib; // backup for isr
    }
    if (isl) {
        bool nt = !(code&Vsplit::T_LSAME);
        bool ns = !(code&Vsplit::S_LSAME);
        if (nt && ns) {
            add_zero(_wedges[wvtfl].attrib, vspl.ar_wad[lnum++]);
            add_zero(_wedges[wvsfl].attrib, vspl.ar_wad[lnum++]);
        } else {
            switch (ii) {
             bcase 2:
                if (ns) _wedges[wvsfl].attrib = _wedges[wvtfl].attrib;
                // remove !ns?: test below?
                add(_wedges[wvtfl].attrib, _wedges[!ns ? wvsfl : wvtfl].attrib, vspl.ar_wad[lnum++]);
             bcase 0:
                if (nt) _wedges[wvtfl].attrib = _wedges[wvsfl].attrib;
                add(_wedges[wvsfl].attrib, _wedges[!nt ? wvtfl : wvsfl].attrib, vspl.ar_wad[lnum++]);
             bcase 1:
             {
                 const PMWedgeAttribD& wad = vspl.ar_wad[lnum];
                 if (!ns) {
                     const PMWedgeAttrib& wabase = _wedges[wvsfl].attrib;
                     add(_wedges[wvtfl].attrib, wabase, wad);
                     sub_reflect(_wedges[wvsfl].attrib, wabase, wad);
                 } else {
                     const PMWedgeAttrib& wabase = _wedges[wvtfl].attrib;
                     sub_reflect(_wedges[wvsfl].attrib, wabase, wad);
                     add(_wedges[wvtfl].attrib, wabase, wad);
                 }
                 lnum++;
             }
             bdefault: assertnever("");
            }
        }
    }
    if (isr) {
        bool nt = !(code&Vsplit::T_RSAME);
        bool ns = !(code&Vsplit::S_RSAME);
        bool ut = !(code&Vsplit::T_CSAME);
        bool us = !(code&Vsplit::S_CSAME);
        if (nt && ns) {
            if (ut) add_zero(_wedges[wvtfr].attrib, vspl.ar_wad[lnum++]);
            if (us) add_zero(_wedges[wvsfr].attrib, vspl.ar_wad[lnum++]);
        } else {
            switch (ii) {
             bcase 2:
                if (us && ns) _wedges[wvsfr].attrib = awvtfr;
                if (ut) add(_wedges[wvtfr].attrib, (!ns ? awvsfr : awvtfr), vspl.ar_wad[lnum++]);
             bcase 0:
                if (ut && nt) _wedges[wvtfr].attrib = awvsfr;
                if (us) add(_wedges[wvsfr].attrib, (!nt ? awvtfr : awvsfr), vspl.ar_wad[lnum++]);
             bcase 1:
             {
                 if (!ns) {
                     const PMWedgeAttrib& wabase = awvsfr;
                     if (ut) add(_wedges[wvtfr].attrib, wabase, vspl.ar_wad[lnum]);
                     if (us) sub_reflect(_wedges[wvsfr].attrib, wabase, vspl.ar_wad[lnum]);
                 } else {
                     const PMWedgeAttrib& wabase = awvtfr;
                     if (us) sub_reflect(_wedges[wvsfr].attrib, wabase, vspl.ar_wad[lnum]);
                     if (ut) add(_wedges[wvtfr].attrib, wabase, vspl.ar_wad[lnum]);
                 }
                 if (ut || us) lnum++;
             }
             bdefault: assertnever("");
            }
        }
    }
    if (code&Vsplit::L_NEW)
        add_zero(_wedges[wvlfl].attrib, vspl.ar_wad[lnum++]);
    if (code&Vsplit::R_NEW)
        add_zero(_wedges[wvrfr].attrib, vspl.ar_wad[lnum++]);
    ASSERTX(lnum==vspl.ar_wad.num());
    ASSERTX(!isl || (attrib_ok(_wedges[wvtfl].attrib), true));
    ASSERTX(!isl || (attrib_ok(_wedges[wvsfl].attrib), true));
    ASSERTX(!isl || (attrib_ok(_wedges[wvlfl].attrib), true));
    ASSERTX(!isr || (attrib_ok(_wedges[wvtfr].attrib), true));
    ASSERTX(!isr || (attrib_ok(_wedges[wvsfr].attrib), true));
    ASSERTX(!isr || (attrib_ok(_wedges[wvrfr].attrib), true));
    // Deal with ancestry
    if (ancestry) {
        apply_vsplit_ancestry(ancestry, vs, isr, onumwedges, code, wvlfl, wvrfr, wvsfl, wvsfr, wvtfl, wvtfr);
    }
    // Final check.
#if defined(HH_DEBUG)
    {
        int wvsflo = flccw==k_undefined ? k_undefined : get_wvf(vs, flccw);
        int wvsfro = frclw==k_undefined ? k_undefined : get_wvf(vs, frclw);
        assertx((code&Vsplit::S_MASK)==unsigned((wvsfl==wvsflo ? Vsplit::S_LSAME : 0u) |
                                                (wvsfr==wvsfro ? Vsplit::S_RSAME : 0u) |
                                                (wvsfl==wvsfr  ? Vsplit::S_CSAME : 0u)));
        int wvtflo = flclw==k_undefined ? k_undefined : get_wvf(vt, flclw);
        int wvtfro = frccw==k_undefined ? k_undefined : get_wvf(vt, frccw);
        assertx((code&Vsplit::T_MASK)==unsigned((wvtfl==wvtflo ? Vsplit::T_LSAME : 0u) |
                                                (wvtfr==wvtfro ? Vsplit::T_RSAME : 0u) |
                                                (wvtfl==wvtfr  ? Vsplit::T_CSAME : 0u)));
    }
#endif
 GOTO_VSPLIT_WAD2:
    void();                     // empty statement
    // ok();
}

void AWMesh::apply_vsplit_private(const Vsplit& vspl, const PMeshInfo& pminfo, Ancestry* ancestry) {
    apply_vsplit(vspl, pminfo, ancestry);
}

void AWMesh::apply_vsplit_ancestry(Ancestry* ancestry, int vs, bool isr, int onumwedges, int code,
                                   int wvlfl, int wvrfr, int wvsfl, int wvsfr, int wvtfl, int wvtfr) {
    const bool isl = true;
    // Vertex ancestry.
    {
        Array<PMVertexAttrib>& vancestry = ancestry->_vancestry;
        int vi = vancestry.add(1);
        // ASSERTX(vi==vt);
        vancestry[vi] = vancestry[vs];
    }
    // Wedge ancestry.
    Array<PMWedgeAttrib>& wancestry = ancestry->_wancestry;
    wancestry.resize(_wedges.num());
    // If ambiguities in inside wedges, set no ancestor.
    if (0) {
    } else if (isr && wvtfl!=wvtfr && wvsfl==wvsfr && wvsfl>=onumwedges && wvtfl<onumwedges && wvtfr<onumwedges) {
        wancestry[wvsfl] = _wedges[wvsfl].attrib;
    } else if (isr && wvsfl!=wvsfr && wvtfl==wvtfr && wvtfl>=onumwedges && wvsfl<onumwedges && wvsfr<onumwedges) {
        wancestry[wvtfl] = _wedges[wvtfl].attrib;
    } else {
        if (isl) {
            int nwvsfl = wvsfl>=onumwedges;
            int nwvtfl = wvtfl>=onumwedges;
            if (nwvsfl) wancestry[wvsfl] = nwvtfl ? _wedges[wvsfl].attrib : wancestry[wvtfl];
            if (nwvtfl) wancestry[wvtfl] = nwvsfl ? _wedges[wvtfl].attrib : wancestry[wvsfl];
        }
        if (isr) {
            int nwvsfr = wvsfr>=onumwedges;
            int nwvtfr = wvtfr>=onumwedges;
            if (nwvsfr) wancestry[wvsfr] = nwvtfr ? _wedges[wvsfr].attrib : wancestry[wvtfr];
            if (nwvtfr) wancestry[wvtfr] = nwvsfr ? _wedges[wvtfr].attrib : wancestry[wvsfr];
        }
    }
    if (code&Vsplit::L_NEW)
        wancestry[wvlfl] = _wedges[wvlfl].attrib;
    if (code&Vsplit::R_NEW)
        wancestry[wvrfr] = _wedges[wvrfr].attrib;
}

void AWMesh::undo_vsplit(const Vsplit& vspl, const PMeshInfo& pminfo) {
    if (0) SHOW("**ecol");
    ASSERTX(_faces.ok(vspl.flclw));
    unsigned code = vspl.code;
    int ii = (code&Vsplit::II_MASK)>>Vsplit::II_SHIFT;
    ASSERTX(ii>=0 && ii<=2);
    const bool isl = true; bool isr;
    int fl, fr;
    if (vspl.vlr_offset1>1) {
        isr = true;
        fl = _faces.num()-2;
        fr = _faces.num()-1;
        ASSERTX(fl>=0 && fr>=0);
    } else {
        isr = false;
        fl = _faces.num()-1;
        fr = k_undefined;
        ASSERTX(fl>=0);
    }
    // Get wedges in neighborhood.
    int wvsfl, wvtfl;
    int wvsfr, wvtfr;
    wvsfl = _faces[fl].wedges[0];
    wvtfl = _faces[fl].wedges[1];
    if (!isr) {
        wvsfr = k_undefined; wvtfr = k_undefined;
    } else {
        wvsfr = _faces[fr].wedges[0];
        wvtfr = _faces[fr].wedges[2];
    }
    ASSERTX(!isl || (wvsfl>=0 && wvtfl>=0));
    ASSERTX(!isr || (wvsfr>=0 && wvtfr>=0));
    int vs = _wedges[wvsfl].vertex;
    int vt = _vertices.num()-1;
    ASSERTX(!isl || _wedges[wvsfl].vertex==vs);
    ASSERTX(!isr || _wedges[wvsfr].vertex==vs);
    ASSERTX(!isl || _wedges[wvtfl].vertex==vt);
    ASSERTX(!isr || _wedges[wvtfr].vertex==vt);
    // Get adjacent faces and wedges on left and right.
    // really needed?
    int flccw, flclw;               // either (not both) may be k_undefined
    int frccw, frclw;               // either (or both) may be k_undefined
    // Also find index of vs within those adjacent faces
    int jlccw, jlclw, jrccw, jrclw; // only defined if faces exist
    dummy_init(jlccw, jlclw, jrccw, jrclw);
    int wlccw, wlclw, wrccw, wrclw; // k_undefined if faces does not exist
    if (isl) {
        flccw = _fnei[fl].faces[1];
        flclw = _fnei[fl].faces[0];
        if (flccw==k_undefined) {
            wlccw = k_undefined;
        } else {
            jlccw = get_jvf(vs, flccw);
            wlccw = _faces[flccw].wedges[jlccw];
        }
        ASSERTX((flclw==k_undefined)==(vspl.vlr_offset1==0));
        if (flclw==k_undefined) {
            wlclw = k_undefined;
        } else {
            jlclw = get_jvf(vt, flclw);
            wlclw = _faces[flclw].wedges[jlclw];
        }
    }
    if (!isr) {
        frccw = k_undefined; frclw = k_undefined; wrccw = k_undefined; wrclw = k_undefined;
    } else {
        frccw = _fnei[fr].faces[0];
        frclw = _fnei[fr].faces[2];
        if (frccw==k_undefined) {
            wrccw = k_undefined;
        } else {
            jrccw = get_jvf(vt, frccw);
            wrccw = _faces[frccw].wedges[jrccw];
        }
        if (frclw==k_undefined) {
            wrclw = k_undefined;
        } else {
            jrclw = get_jvf(vs, frclw);
            wrclw = _faces[frclw].wedges[jrclw];
        }
    }
    bool thru_l = wlccw==wvsfl && wlclw==wvtfl;
    bool thru_r = wrclw==wvsfr && wrccw==wvtfr;
#if defined(HH_DEBUG)
    {
        int vs_index = (code&Vsplit::VSINDEX_MASK)>>Vsplit::VSINDEX_SHIFT;
        assertx(vspl.flclw==(vspl.vlr_offset1==0 ? flccw : flclw));
        if (vspl.vlr_offset1==0)
            assertx(_wedges[_faces[flccw].wedges[vs_index]].vertex==vs);
        else
            assertx(_wedges[_faces[flclw].wedges[vs_index]].vertex==vt);
        assertx(fr==_fnei[fl].faces[2]);
        if (fr>=0) assertx(_fnei[fr].faces[1]==fl);
        //
        if (!isr) assertx((code&Vsplit::S_RSAME) && !(code&Vsplit::T_CSAME));
        if (!isr) assertx((code&Vsplit::T_RSAME) && !(code&Vsplit::T_CSAME));
        if (isl) {
            assertx((wlccw==wvsfl)==!!(code&Vsplit::S_LSAME));
            assertx((wlclw==wvtfl)==!!(code&Vsplit::T_LSAME));
        }
        if (isr) {
            assertx((wrclw==wvsfr)==!!(code&Vsplit::S_RSAME));
            assertx((wrccw==wvtfr)==!!(code&Vsplit::T_RSAME));
        }
        assertx((wvsfl==wvsfr)==!!(code&Vsplit::S_CSAME));
        assertx((wvtfl==wvtfr)==!!(code&Vsplit::T_CSAME));
    }
#endif
    // Update adjacency information.
    if (flccw>=0) _fnei[flccw].faces[mod3(jlccw+2)] = flclw;
    if (flclw>=0) _fnei[flclw].faces[mod3(jlclw+1)] = flccw;
    if (frccw>=0) _fnei[frccw].faces[mod3(jrccw+2)] = frclw;
    if (frclw>=0) _fnei[frclw].faces[mod3(jrclw+1)] = frccw;
    // Propagate wedges id's across collapsed faces if can go thru.
    int ffl = flclw, ffr = frccw;
    int jjl = jlclw, jjr = jrccw;
    int* pwwl; dummy_init(pwwl);
    if (ffl>=0) pwwl = &_faces[ffl].wedges[jjl];
    else if (k_debug) pwwl = reinterpret_cast<int*>(intptr_t{k_undefined});
    if (thru_l) {               // first go clw
        ASSERTX(*pwwl==wlclw);
        ASSERTX(wlccw>=0);
        for (;;) {
            *pwwl = wlccw;
            if (ffl==ffr) {
                ffl = ffr = k_undefined; // all wedges seen
                break;
            }
            ffl = _fnei[ffl].faces[mod3(jjl+2)];
            if (ffl<0) break;
            jjl = get_jvf(vt, ffl);
            pwwl = &_faces[ffl].wedges[jjl];
            if (*pwwl!=wlclw) break;
        }
    }
    if (ffr>=0 && thru_r) {     // now go ccw from other side
        int* pw = &_faces[ffr].wedges[jjr];
        ASSERTX(*pw==wrccw);
        ASSERTX(wrclw>=0);
        for (;;) {
            *pw = wrclw;
            if (ffr==ffl) {
                ffl = ffr = k_undefined; // all wedges seen
                break;
            }
            ffr = _fnei[ffr].faces[mod3(jjr+1)];
            if (ffr<0) break;
            jjr = get_jvf(vt, ffr);
            pw = &_faces[ffr].wedges[jjr];
            if (*pw!=wrccw) break;
        }
    }
    // Identify those wedges that will need to be updated to vs.
    //  (wmodif may contain some duplicates)
    PArray<int,10> ar_wmodif;
    if (ffl>=0) {
        for (;;) {
            int w = *pwwl;
            ar_wmodif.push(w);
            if (ffl==ffr) { ffl = ffr = k_undefined; break; }
            ffl = _fnei[ffl].faces[mod3(jjl+2)];
            if (ffl<0) break;
            jjl = get_jvf(vt, ffl);
            pwwl = &_faces[ffl].wedges[jjl];
        }
    }
    ASSERTX(ffl<0 && ffr<0);
    // Update wedge vertices to vs.
    for (int w : ar_wmodif) { _wedges[w].vertex = vs; }
    // Update vertex attributes.
    {
        PMVertexAttrib& vas = _vertices[vs].attrib;
        PMVertexAttrib& vat = _vertices[vt].attrib;
        switch (ii) {
         bcase 2:
            sub(vas, vas, vspl.vad_small);
         bcase 0:
            sub(vas, vat, vspl.vad_small);
         bcase 1:
            if (0) {
                PMVertexAttrib vam;
                interp(vam, vas, vat, 0.5f);
                sub(vas, vam, vspl.vad_small);
            } else {
                // slightly faster
                sub(vas, vat, vspl.vad_large);
                sub(vas, vas, vspl.vad_small);
            }
         bdefault: assertnever("");
        }
    }
    // Udpate wedge attributes.
    PMWedgeAttrib awvtfr, awvsfr; dummy_init(awvtfr, awvsfr);
    bool problem = false;
    if (pminfo._has_wad2) {
        assertx(vspl.ar_wad.num()==2);
        sub_noreflect(_wedges[wvsfl].attrib, _wedges[wvsfl].attrib, vspl.ar_wad[1]);
        _wedges[wvtfl].attrib = _wedges[wvsfl].attrib;
        goto GOTO_UNDO_WAD2;
    }
    //  they are currently predicted exactly.
    if (isr) {
        awvtfr = _wedges[wvtfr].attrib;
        awvsfr = _wedges[wvsfr].attrib;
    }
    if (isl) {
        bool nt = !(code&Vsplit::T_LSAME);
        bool ns = !(code&Vsplit::S_LSAME);
        if (nt && ns) {
            problem = true;
        } else {
            switch (ii) {
             bcase 2:
                if (!thru_l) _wedges[wvtfl].attrib = _wedges[wvsfl].attrib;
             bcase 0:
                _wedges[wvsfl].attrib = _wedges[wvtfl].attrib;
             bcase 1:
             {
                 PMWedgeAttrib wa;
                 if (0) {
                     interp(wa, _wedges[wvsfl].attrib, _wedges[wvtfl].attrib, 0.5f);
                 } else {
                     // faster because avoid normalization of interp()
                     const int lnum = 0;
                     const PMWedgeAttribD& wad = vspl.ar_wad[lnum];
                     sub_noreflect(wa, _wedges[wvtfl].attrib, wad);
                 }
                 _wedges[wvsfl].attrib = wa;
                 if (!thru_l) _wedges[wvtfl].attrib = wa;
             }
             bdefault: assertnever("");
            }
        }
    }
    if (isr) {
        bool nt = !(code&Vsplit::T_RSAME);
        bool ns = !(code&Vsplit::S_RSAME);
        bool ut = !(code&Vsplit::T_CSAME);
        bool us = !(code&Vsplit::S_CSAME);
        if (problem || us || ut) {
            switch (ii) {
             bcase 2:
                // If thru_r, then wvtfr & wrccw no longer exist.
                // This may be duplicating some work already done for isl.
                if (!nt && !thru_r)
                    _wedges[wvtfr].attrib = awvsfr;
             bcase 0:
                // This may be duplicating some work already done for isl.
                if (!ns)
                    _wedges[wvsfr].attrib = awvtfr;
             bcase 1:
             {
                 PMWedgeAttrib wa;
                 interp(wa, awvsfr, awvtfr, 0.5f);
                 if (!ns) _wedges[wvsfr].attrib = wa;
                 if (!nt && !thru_r) _wedges[wvtfr].attrib = wa;
             }
             bdefault: assertnever("");
            }
        }
    }
    ASSERTX(wlccw<0 || (attrib_ok(_wedges[wlccw].attrib), true));
    ASSERTX(wlclw<0 || (attrib_ok(_wedges[wlclw].attrib), true));
    ASSERTX(wrccw<0 || (attrib_ok(_wedges[wrccw].attrib), true));
    ASSERTX(wrclw<0 || (attrib_ok(_wedges[wrclw].attrib), true));
 GOTO_UNDO_WAD2:
    // Remove faces.
    _faces.resize(fl), _fnei.resize(fl); // remove 1 or 2, !remember _fnei
    // Remove vertex.
    _vertices.sub(1);           // remove 1 vertex (vt)
    // Remove wedges.
    // optimize: construct static const lookup table on (S_MASK | T_MASK)
    bool was_wnl = isl && (code&Vsplit::T_LSAME) && (code&Vsplit::S_LSAME);
    bool was_wnr = isr && (code&Vsplit::T_RSAME) && (code&Vsplit::S_RSAME) &&
        !(was_wnl && (code&Vsplit::T_CSAME));
    bool was_wntl = isl && (!(code&Vsplit::T_LSAME) && (!(code&Vsplit::T_CSAME) || !(code&Vsplit::T_RSAME)));
    bool was_wntr = isr && (!(code&Vsplit::T_CSAME) && !(code&Vsplit::T_RSAME));
    bool was_wnsl = isl && (!(code&Vsplit::S_LSAME) && (!(code&Vsplit::S_CSAME) || !(code&Vsplit::S_RSAME)));
    bool was_wnsr = isr && (!(code&Vsplit::S_CSAME) && !(code&Vsplit::S_RSAME));
    bool was_wnol = isl && (code&Vsplit::L_NEW);
    bool was_wnor = isr && (code&Vsplit::R_NEW);
    int nwr = (int(was_wnl)+int(was_wnr)+int(was_wntl)+int(was_wntr)+int(was_wnsl)+int(was_wnsr)+
               int(was_wnol)+int(was_wnor));
#if defined(HH_DEBUG)
    {
        // nwr==0 possible when ws==LSAME | CSAME and wt==CSAME | RSAME
        assertx(nwr>=0 && nwr<=6);
        // Verify count with _wedges array.
        int cur = _wedges.num();
        if (was_wnor) --cur;    // "wvrfr"
        if (was_wnol) --cur;    // "wvlfl"
        if (was_wnsr) assertx(--cur==wvsfr);
        if (was_wnsl) assertx(--cur==wvsfl);
        if (was_wntr) assertx(--cur==wvtfr);
        if (was_wntl) assertx(--cur==wvtfl);
        if (was_wnr ) assertx(--cur==wrccw);
        if (was_wnl ) assertx(--cur==wlclw);
    }
#endif
    _wedges.sub(nwr);
    // Final check.
    // ok();
}

void AWMesh::ok() const {
    WMesh::ok();
    assertx(_fnei.num()==_faces.num());
    for_int(f, _faces.num()) {
        Set<int> setfnei;
        for_int(j, 3) {
            int ff = _fnei[f].faces[j];
            if (ff==k_undefined) continue;
            assertx(_faces.ok(ff));
            assertx(setfnei.add(ff)); // no valence_2 vertices
            int v1 = _wedges[_faces[f].wedges[mod3(j+1)]].vertex;
            int v2 = _wedges[_faces[f].wedges[mod3(j+2)]].vertex;
            assertx(v1!=v2);
            int ov1 = get_jvf(v1, ff);
            int ov2 = get_jvf(v2, ff);
            assertx(_fnei[ff].faces[mod3(ov1+1)]==f);
            assertx(mod3(ov1-ov2+3)==1);
        }
    }
    // Check that wedges are consecutive around each vertex.
    {
        int count = 0;
        // vertex -> most clw face (or beg of wedge (or any face))
        Array<int> mvf(_vertices.num(), k_undefined);
        for_int(f, _faces.num()) {
            for_int(j, 3) {
                int w = _faces[f].wedges[j];
                int v = _wedges[w].vertex;
                int fclw = _fnei[f].faces[mod3(j+2)];
                bool is_beg_wedge = fclw>=0 && get_wvf(v, fclw)!=w;
                if (is_beg_wedge || mvf[v]==k_undefined) mvf[v] = f;
            }
        }
        for_int(f, _faces.num()) {
            for_int(j, 3) {
                int w = _faces[f].wedges[j];
                int v = _wedges[w].vertex;
                int fclw = _fnei[f].faces[mod3(j+2)];
                bool is_most_clw = fclw==k_undefined;
                if (is_most_clw) mvf[v] = f;
            }
        }
        for_int(v, _vertices.num()) {
            int f0 = mvf[v];
            int wp = k_undefined;
            Set<int> setw;
            for (int f = f0; ; ) {
                int j = get_jvf(v, f);
                int w = _faces[f].wedges[j];
                if (w!=wp) {
                    assertx(setw.add(w));
                    wp = w;
                }
                count++;
                f = _fnei[f].faces[mod3(j+1)]; // go ccw
                if (f==k_undefined || f==f0) break;
            }
        }
        assertx(count==_faces.num()*3);
    }
}

void AWMesh::construct_adjacency() {
    _fnei.init(_faces.num());
    Map<std::pair<int,int>, int> mvv_face; // (Vertex,Vertex) -> Face
    for_int(f, _faces.num()) {
        for_int(j, 3) {
            int j1 = mod3(j+1), j2 = mod3(j+2);
            int v0 = _wedges[_faces[f].wedges[j1]].vertex;
            int v1 = _wedges[_faces[f].wedges[j2]].vertex;
            mvv_face.enter(std::make_pair(v0, v1), f);
        }
    }
    for_int(f, _faces.num()) {
        for_int(j, 3) {
            int j1 = mod3(j+1), j2 = mod3(j+2);
            int v0 = _wedges[_faces[f].wedges[j1]].vertex;
            int v1 = _wedges[_faces[f].wedges[j2]].vertex;
            bool present; int fn = mvv_face.retrieve(std::make_pair(v1, v0), present);
            _fnei[f].faces[j] = present ? fn : k_undefined;
        }
    }
}

// *** PMesh

PMesh::PMesh() {
    _info._tot_nvsplits = 0;
    _info._full_nvertices = 0;
    _info._full_nwedges = 0;
    _info._full_nfaces = 0;
    _info._full_bbox[0] = Point(BIGFLOAT, BIGFLOAT, BIGFLOAT);
    _info._full_bbox[1] = Point(BIGFLOAT, BIGFLOAT, BIGFLOAT);
}

void PMesh::read(std::istream& is) {
    PMeshRStream pmrs(is, this);
    pmrs.read_base_mesh();
    for (;;) {
        if (!pmrs.next_vsplit()) break;
    }
}

void PMesh::write(std::ostream& os) const {
    os << "PM\n";
    os << "version=2\n";
    os << sform("nvsplits=%d nvertices=%d nwedges=%d nfaces=%d\n",
                _info._tot_nvsplits, _info._full_nvertices, _info._full_nwedges, _info._full_nfaces);
    const Bbox& bb = _info._full_bbox;
    os << sform("bbox %g %g %g  %g %g %g\n", bb[0][0], bb[0][1], bb[0][2], bb[1][0], bb[1][1], bb[1][2]);
    os << sform("has_rgb=%d\n", _info._has_rgb);
    os << sform("has_uv=%d\n", _info._has_uv);
    os << sform("has_resid=%d\n", _info._has_resid);
    if (_info._has_wad2) os << sform("has_wad2=%d\n", _info._has_wad2);
    os << "PM base mesh:\n";
    _base_mesh.write(os, _info);
    for_int(i, _vsplits.num()) {
        _vsplits[i].write(os, _info);
    }
    os << '\xFF';
    os << "End of PM\n";
    assertx(os);
}

PMeshInfo PMesh::read_header(std::istream& is) {
    PMeshInfo pminfo;
    for (string sline; ; ) {
        assertx(my_getline(is, sline));
        if (sline=="" || sline[0]=='#') continue;
        assertx(sline=="PM"); break;
    }
    // default for version 1 compatibility
    pminfo._read_version = 1;
    // default for version 0 compatibility
    pminfo._has_rgb = false;
    pminfo._has_uv = true;
    pminfo._has_resid = false;
    pminfo._has_wad2 = false;
    //
    pminfo._tot_nvsplits = 0;
    pminfo._full_nvertices = 0;
    pminfo._full_nwedges = 0;
    pminfo._full_nfaces = 0;
    pminfo._full_bbox[0] = Point(BIGFLOAT, BIGFLOAT, BIGFLOAT);
    pminfo._full_bbox[1] = Point(BIGFLOAT, BIGFLOAT, BIGFLOAT);
    for (string sline; ; ) {
        assertx(my_getline(is, sline));
        const char* s2 = sline.c_str();
        if (0) {
        } else if (!strncmp(s2, "version=", 8)) {
            assertx(sscanf(s2, "version=%d", &pminfo._read_version)==1);
        } else if (!strncmp(s2, "nvsplits=", 9)) {
            assertx(sscanf(s2, "nvsplits=%d nvertices=%d nwedges=%d nfaces=%d",
                           &pminfo._tot_nvsplits, &pminfo._full_nvertices,
                           &pminfo._full_nwedges, &pminfo._full_nfaces)==4);
        } else if (!strncmp(s2, "bbox ", 5)) {
            Bbox& bb = pminfo._full_bbox;
            assertx(sscanf(s2, "bbox %g %g %g  %g %g %g",
                           &bb[0][0], &bb[0][1], &bb[0][2],
                           &bb[1][0], &bb[1][1], &bb[1][2])==6);
        } else if (!strncmp(s2, "has_rgb=", 8)) {
            int i; assertx(sscanf(s2, "has_rgb=%d", &i)==1); pminfo._has_rgb = narrow_cast<bool>(i);
        } else if (!strncmp(s2, "has_uv=", 7)) {
            int i; assertx(sscanf(s2, "has_uv=%d", &i)==1); pminfo._has_uv = narrow_cast<bool>(i);
        } else if (!strncmp(s2, "has_resid=", 10)) {
            int i; assertx(sscanf(s2, "has_resid=%d", &i)==1); pminfo._has_resid = narrow_cast<bool>(i);
        } else if (!strncmp(s2, "has_wad2=", 9)) {
            int i; assertx(sscanf(s2, "has_wad2=%d", &i)==1); pminfo._has_wad2 = narrow_cast<bool>(i);
        } else if (!strcmp(s2, "PM base mesh:")) {
            break;
        } else {
            Warning("PMesh header string unknown");
            showf("PMesh header string not recognized '%s'\n", s2);
        }
    }
    assertw(pminfo._tot_nvsplits);
    assertw(pminfo._full_nvertices);
    assertw(pminfo._full_nwedges);
    assertw(pminfo._full_nfaces);
    assertw(pminfo._full_bbox[0][0]!=BIGFLOAT);
    return pminfo;
}

bool PMesh::at_trailer(std::istream& is) {
    return is.peek()==k_magic_first_byte;
}

void PMesh::truncate_beyond(PMeshIter& pmi) {
    PMeshRStream& pmrs = pmi._pmrs;
    _vsplits.resize(pmrs._vspliti);
    _info._tot_nvsplits = _vsplits.num();
    _info._full_nvertices = pmi._vertices.num();
    _info._full_nwedges = pmi._wedges.num();
    _info._full_nfaces = pmi._faces.num();
    pmrs._info = _info;
    // really, should update all PMeshRStreams which are open on PMesh.
}

void PMesh::truncate_prior(PMeshIter& pmi) {
    PMeshRStream& pmrs = pmi._pmrs;
    _base_mesh = pmi;
    _vsplits.erase(0, pmrs._vspliti);
    _info._tot_nvsplits = _vsplits.num();
    pmrs._info = _info;
    pmrs._vspliti = 0;
    // really, should update all PMeshRStreams which are open on PMesh.
}

// *** PMeshRStream

PMeshRStream::PMeshRStream(const PMesh& pm) : _is(nullptr), _pm(const_cast<PMesh*>(&pm)) {
    _info = _pm->_info;
}

PMeshRStream::PMeshRStream(std::istream& is, PMesh* ppm_construct) : _is(&is), _pm(ppm_construct) {
    _info = PMesh::read_header(*_is);
    if (_pm) _pm->_info = _info;
}

PMeshRStream::~PMeshRStream() {
    // Should not check for PMesh trailer here since not all records may have been read.
}

void PMeshRStream::read_base_mesh(AWMesh* bmesh) {
    assertx(_vspliti==-1);
    _vspliti = 0;
    if (!_is) {
        if (bmesh) *bmesh = _pm->_base_mesh;
    } else {
        if (!_pm & !bmesh) { Warning("strange, why are we doing this?"); }
        unique_ptr<AWMesh> tbmesh = !_pm && !bmesh ? make_unique<AWMesh>() : nullptr;
        AWMesh& rbmesh = _pm ? _pm->_base_mesh : bmesh ? *bmesh : *tbmesh;
        rbmesh.read(*_is, _info);
        if (_pm && bmesh) *bmesh = _pm->_base_mesh;
    }
}

const AWMesh& PMeshRStream::base_mesh() {
    if (_vspliti==-1) {
        if (!_pm) {
            read_base_mesh(&_lbase_mesh);
        } else {
            read_base_mesh(nullptr);
        }
    }
    return _pm ? _pm->_base_mesh : _lbase_mesh;
}

const Vsplit* PMeshRStream::peek_next_vsplit() {
    assertx(_vspliti>=0);
    if (_pm) {
        if (_vspliti<_pm->_vsplits.num())
            return &_pm->_vsplits[_vspliti];
        if (!_is) return nullptr; // end of array
    } else if (_vspl_ready) {
        // have buffer record
        return &_tmp_vspl;
    }
    assertx(*_is);
    if (PMesh::at_trailer(*_is)) return nullptr;
    Vsplit* pvspl;
    if (_pm) {
        if (!_pm->_vsplits.num())
            _pm->_vsplits.reserve(_pm->_info._tot_nvsplits);
        _pm->_vsplits.access(_vspliti);
        pvspl = &_pm->_vsplits[_vspliti];
    } else {
        pvspl = &_tmp_vspl;
        _vspl_ready = true;
    }
    pvspl->read(*_is, _info);
    return pvspl;
}

const Vsplit* PMeshRStream::next_vsplit() {
    assertx(_vspliti>=0);
    if (_pm) {
        if (_vspliti<_pm->_vsplits.num()) {
            _vspliti++;
            return &_pm->_vsplits[_vspliti-1];
        }
        if (!_is) return nullptr; // end of array
    } else if (_vspl_ready) {
        // use up buffer record
        _vspl_ready = false;
        return &_tmp_vspl;
    }
    assertx(*_is);
    if (PMesh::at_trailer(*_is)) return nullptr;
    Vsplit* pvspl;
    if (_pm) {
        if (!_pm->_vsplits.num()) _pm->_vsplits.reserve(_pm->_info._tot_nvsplits);
        _vspliti++;
        _pm->_vsplits.resize(_vspliti);
        pvspl = &_pm->_vsplits[_vspliti-1];
    } else {
        pvspl = &_tmp_vspl;
    }
    pvspl->read(*_is, _info);
    return pvspl;
}

const Vsplit* PMeshRStream::prev_vsplit() {
    assertx(_vspliti>=0);
    assertx(is_reversible());      // die if !_pm
    if (!_vspliti) return nullptr; // end of array
    --_vspliti;
    return &_pm->_vsplits[_vspliti];
}

// *** PMeshIter

PMeshIter::PMeshIter(PMeshRStream& pmrs) : _pmrs(pmrs) {
    _pmrs.read_base_mesh(this);
}

bool PMeshIter::next_ancestry(Ancestry* ancestry) {
    const Vsplit* vspl = _pmrs.next_vsplit();
    if (!vspl)
        return false;
    apply_vsplit(*vspl, rstream()._info, ancestry);
    return true;
}

bool PMeshIter::prev() {
    const Vsplit* vspl = _pmrs.prev_vsplit();
    if (!vspl) return false;
    undo_vsplit(*vspl, rstream()._info);
    return true;
}

bool PMeshIter::goto_nvertices_ancestry(int nvertices, Ancestry* ancestry) {
    // If have PM and about to go to full mesh, reserve.
    if (_pmrs._pm) {
        const PMesh& pm = *_pmrs._pm;
        if (nvertices>=pm._info._full_nvertices) {
            _vertices.reserve(pm._info._full_nvertices);
            _wedges.reserve(pm._info._full_nwedges);
            _faces.reserve(pm._info._full_nfaces);
        }
    }
    for (;;) {
        int cn = _vertices.num();
        if (cn<nvertices) {
            if (!next_ancestry(ancestry)) return false;
        } else if (cn>nvertices) {
            assertx(!ancestry);
            if (!prev()) return false;
        } else {
            break;
        }
    }
    return true;
}

bool PMeshIter::goto_nfaces_ancestry(int nfaces, Ancestry* ancestry) {
    // If have PM and about to go to full mesh, reserve.
    if (_pmrs._pm) {
        const PMesh& pm = *_pmrs._pm;
        if (nfaces>=pm._info._full_nfaces) {
            _vertices.reserve(pm._info._full_nvertices);
            _wedges.reserve(pm._info._full_nwedges);
            _faces.reserve(pm._info._full_nfaces);
        }
    }
    if (_faces.num()<nfaces) {
        while (_faces.num()<nfaces-1) {
            if (!next_ancestry(ancestry)) return false;
        }
        if (_faces.num()==nfaces-1) {
            // Avoid over-shooting since may not be reversible,
            //  so peek at next vsplit and apply it if it adds only 1 face.
            const Vsplit* pvspl = _pmrs.peek_next_vsplit();
            if (pvspl && !pvspl->adds_two_faces()) {
                assertx(next_ancestry(ancestry));
                assertx(_faces.num()==nfaces);
            }
        }
    } else if (_faces.num()>nfaces) {
        assertx(!ancestry);
        while (_faces.num()>nfaces) {
            if (!prev()) return false;
        }
        // Favor (nfaces-1) over (nfaces+1).
    }
    return true;
}

// *** Geomorph

bool Geomorph::construct(PMeshIter& pmi, EWant want, int num) {
    // Note that this construction is not identical to that in Filterprog.cpp, at least for wedges,
    //  since Filterprog.cpp records for each final wedge its original wid
    //  in M^c, or 0 if the wedge was introduced subsequently.
    // However, a newly added wedge introduced in M^j, j>c, can have further descendants in M^k, k>j,
    //  with different attributes, and these perhaps should be morphed (between their original values in
    //  j and their final values in k).
    // They are morphed here but not in Filterprog.cpp.
    // 970516: this is the reason why I didn't let the Ancestry class contain
    // ancestry relations (and record the attributes of M^c): I would not
    // have available to me the original attributes of an orphan wedge
    // introduced in M^j which was subsequently changed.  The only solution is
    // to carry the attributes explicitly as I currently do,
    // or allocate a separate array to record the attributes of orphan wedges
    // when they are first created, which doesn't seem so nice.
    bool ret = true;
    assertx(!_vertices.num() && !_vgattribs.num() && !_wgattribs.num());
    // Initialize ancestry to current attributes (identity).
    // optimize: could reserve final size for arrays.
    Ancestry ancestry;
    ancestry._vancestry.init(pmi._vertices.num());
    for_int(v, ancestry._vancestry.num()) {
        ancestry._vancestry[v] = pmi._vertices[v].attrib;
    }
    ancestry._wancestry.init(pmi._wedges.num());
    for_int(w, ancestry._wancestry.num()) {
        ancestry._wancestry[w] = pmi._wedges[w].attrib;
    }
    // Perform vsplits.
    switch (want) {
     bcase EWant::vsplits:
        for_int(i, num) {
            if (!pmi.next_ancestry(&ancestry)) { ret = false; break; }
        }
     bcase EWant::nfaces:
        assertx(num>=pmi._faces.num());
        if (!pmi.goto_nfaces_ancestry(num, &ancestry)) ret = false;
     bcase EWant::nvertices:
        assertx(num>=pmi._vertices.num());
        if (!pmi.goto_nvertices_ancestry(num, &ancestry)) ret = false;
     bdefault: assertnever("");
    }
    // Grab final mesh
    static_cast<WMesh&>(*this) = pmi; // copy pmi::AWMesh to this->WMesh
    ASSERTX(_vertices.num()==ancestry._vancestry.num());
    ASSERTX(_wedges.num()==ancestry._wancestry.num());
    // Fill attribute arrays _vattribs and _wattribs.
    {
        int nv = 0;
        for_int(v, ancestry._vancestry.num()) {
            if (compare(ancestry._vancestry[v], _vertices[v].attrib)) nv++;
        }
        _vgattribs.init(nv); nv = 0;
        for_int(v, ancestry._vancestry.num()) {
            if (!compare(ancestry._vancestry[v], _vertices[v].attrib))
                continue;
            _vgattribs[nv].vertex = v;
            _vgattribs[nv].attribs[0] = ancestry._vancestry[v];
            _vgattribs[nv].attribs[1] = _vertices[v].attrib;
            nv++;
        }
        assertx(nv==_vgattribs.num());
    }
    {
        int nw = 0;
        for_int(w, ancestry._wancestry.num()) {
            if (compare(ancestry._wancestry[w], _wedges[w].attrib)) nw++;
        }
        _wgattribs.init(nw); nw = 0;
        for_int(w, ancestry._wancestry.num()) {
            if (!compare(ancestry._wancestry[w], _wedges[w].attrib))
                continue;
            _wgattribs[nw].wedge = w;
            _wgattribs[nw].attribs[0] = ancestry._wancestry[w];
            _wgattribs[nw].attribs[1] = _wedges[w].attrib;
            nw++;
        }
        assertx(nw==_wgattribs.num());
    }
    return ret;
}

bool Geomorph::construct_next(PMeshIter& pmi, int nvsplits) {
    return construct(pmi, EWant::vsplits, nvsplits);
}

bool Geomorph::construct_goto_nvertices(PMeshIter& pmi, int nvertices) {
    return construct(pmi, EWant::nvertices, nvertices);
}

bool Geomorph::construct_goto_nfaces(PMeshIter& pmi, int nfaces) {
    return construct(pmi, EWant::nfaces, nfaces);
}

void Geomorph::evaluate(float alpha) {
    assertx(alpha>=0.f && alpha<1.f);
    const float f1 = 1.f-alpha;
    for (const PMVertexAttribG& ag : _vgattribs) {
        interp(_vertices[ag.vertex].attrib, ag.attribs[0], ag.attribs[1], f1);
    }
    for (const PMWedgeAttribG& ag : _wgattribs) {
        interp(_wedges[ag.wedge].attrib, ag.attribs[0], ag.attribs[1], f1);
    }
}

// *** SMesh

SMesh::SMesh(const WMesh& wmesh)
    : _materials(wmesh._materials), _vertices(wmesh._wedges.num()), _faces(wmesh._faces.num()) {
    for_int(v, _vertices.num()) {
        int ov = wmesh._wedges[v].vertex;
        _vertices[v].attrib.v = wmesh._vertices[ov].attrib;
        _vertices[v].attrib.w = wmesh._wedges[v].attrib;
    }
    for_int(f, _faces.num()) {
        for_int(j, 3) {
            _faces[f].vertices[j] = wmesh._faces[f].wedges[j];
        }
        _faces[f].attrib = wmesh._faces[f].attrib;
    }
}

void SMesh::extract_gmesh(GMesh& gmesh, int has_rgb, int has_uv) const {
    assertx(!gmesh.num_vertices());
    string str;
    for_int(v, _vertices.num()) {
        Vertex gv = gmesh.create_vertex();
        ASSERTX(gmesh.vertex_id(gv)==v+1);
        gmesh.set_point(gv, _vertices[v].attrib.v.point);
        const Vector& nor = _vertices[v].attrib.w.normal;
        gmesh.update_string(gv, "normal", csform_vec(str, nor));
        if (has_rgb) {
            const A3dColor& col = _vertices[v].attrib.w.rgb;
            gmesh.update_string(gv, "rgb", csform_vec(str, col));
        }
        if (has_uv) {
            const UV& uv = _vertices[v].attrib.w.uv;
            gmesh.update_string(gv, "uv", csform_vec(str, uv));
        }
    }
    Array<Vertex> gva;
    for_int(f, _faces.num()) {
        gva.init(0);
        for_int(j, 3) {
            int v = _faces[f].vertices[j];
            gva.push(gmesh.id_vertex(v+1));
        }
        Face gf = gmesh.create_face(gva);
        int matid = _faces[f].attrib.matid&~AWMesh::k_Face_visited_mask;
        gmesh.set_string(gf, _materials.get(matid).c_str());
    }
}

// *** SGeomorph

SGeomorph::SGeomorph(const Geomorph& geomorph) : SMesh(geomorph) {
    Array<int> mvmodif(geomorph._vertices.num(), k_undefined);
    Array<int> mwmodif(geomorph._wedges.num(), k_undefined);
    for_int(ovi, geomorph._vgattribs.num()) {
        mvmodif[geomorph._vgattribs[ovi].vertex] = ovi;
    }
    for_int(owi, geomorph._wgattribs.num()) {
        mwmodif[geomorph._wgattribs[owi].wedge] = owi;
    }
    for_int(ow, geomorph._wedges.num()) {
        int ov = geomorph._wedges[ow].vertex;
        int ovi = mvmodif[ov];
        int owi = mwmodif[ow];
        if (ovi==k_undefined && owi==k_undefined) continue;
        int gnum = _vgattribs.add(1); _vgattribs[gnum].vertex = ow;
        for_int(a, 2) {
            _vgattribs[gnum].attribs[a].v = (ovi!=k_undefined
                                             ? geomorph._vgattribs[ovi].attribs[a]
                                             : geomorph._vertices[ov].attrib);
            _vgattribs[gnum].attribs[a].w = (owi!=k_undefined
                                             ? geomorph._wgattribs[owi].attribs[a]
                                             : geomorph._wedges[ow].attrib);
        }
    }
    _vgattribs.shrink_to_fit();
}

void SGeomorph::evaluate(float alpha) {
    assertx(alpha>=0.f && alpha<1.f);
    const float f1 = 1.f-alpha;
    for (const PMSVertexAttribG& ag : _vgattribs) {
        interp(_vertices[ag.vertex].attrib, ag.attribs[0], ag.attribs[1], f1);
    }
}

void SGeomorph::extract_gmesh(GMesh& gmesh, int has_rgb, int has_uv) const {
    SMesh::extract_gmesh(gmesh, has_rgb, has_uv);
    Array<int> mvvg(_vertices.num(), -1);
    for_int(vg, _vgattribs.num()) {
        mvvg[_vgattribs[vg].vertex] = vg;
    }
    string str;
    for_int(v, _vertices.num()) {
        Vertex gv = gmesh.id_vertex(v+1);
        int vg = mvvg[v];
        if (vg>=0) assertx(_vgattribs[vg].vertex==v);
        const PMSVertexAttrib& va0 = vg<0 ? _vertices[v].attrib : _vgattribs[vg].attribs[0];
        const Point& opos = va0.v.point;
        gmesh.update_string(gv, "Opos", csform_vec(str, opos));
        const Vector& onor = va0.w.normal;
        gmesh.update_string(gv, "Onormal", csform_vec(str, onor));
        if (has_rgb) {
            const A3dColor& col = va0.w.rgb;
            gmesh.update_string(gv, "Orgb", csform_vec(str, col));
        }
        if (has_uv) {
            const UV& uv = va0.w.uv;
            gmesh.update_string(gv, "Ouv", csform_vec(str, uv));
        }
    }
}

} // namespace hh
