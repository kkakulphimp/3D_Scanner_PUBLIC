// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "Args.h"
#include "HB.h"
#include "Map.h"
#include "Set.h"
#include "A3dStream.h"
#include "Polygon.h"
#include "HW.h"
#include "HashPoint.h"
#include "HiddenLineRemoval.h"
#include "Array.h"
#include "Set.h"
#include "Postscript.h"
#include "FileIO.h"
#include "GMesh.h"
#include "PArray.h"
#include "MathOp.h"             // floor(Vec<>)
using namespace hh;

namespace g3d {
extern string statefile;
} // namespace g3d

extern float ambient;           // used in G3devent.cpp
float ambient;

namespace {

constexpr int k_max_object = 1024;  // should be >= objects::MAX

const FlagMask fflag_invisible = Mesh::allocate_Face_flag();
// const string k_default_geometry = "700x700+0+0";
const string k_default_geometry = "700x700+130+0";
constexpr float k_default_hither = .01f;
const int interval_check_stop = 1024;
enum ClipConditionCodes { CCN = 0, CCH = 1, CCY = 2, CCR = 4, CCL = 8, CCD = 16, CCU = 32 };

struct DerivedHW : HW {
    DerivedHW() { }
    bool key_press(string s) override;
    void button_press(int butnum, bool pressed, const Vec2<int>& yx) override;
    void wheel_turn(float v) override;
    void draw_window(const Vec2<int>& dims) override;
    void input_received() override              { if (_finpu) _finpu(); }
    void (*_finpu)() {nullptr};
};

DerivedHW hw;

// per object attributes
bool cullface;                  // cull using polygon normal
int culledge;                   // cull using edge normals (3 states)
bool reverse_cull;              // reverse all cull tests
bool highlight_vertices;        // draw segment end points
bool show_sharp;                 // show only sharp edges

bool quickmode;                 // draw quick segments
int quicki;                     // draw every quicki'th segment
bool butquick;                  // do quickmode when button is pressed
bool hlrmode;                   // hidden line removal
bool buthlr;                    // draw hlr when button is not pressed
bool nohash;                    // do not share vertex geometry
bool datastat;
bool fisheye;                   // fisheye view mode
bool silhouette;
float hither;
bool is_yonder;
float yonder;
float zoom;
Frame tcam;
bool (*fkeyp)(const string& s);
void (*fbutp)(int butnum, bool pressed, bool shift, const Vec2<float>& yx);
void (*fwheel)(float v);
void (*fdraw)();

bool is_window;                 // window is open and drawable
Vec2<int> win_dims;             // window dimensions
Frame tcami;
bool lquickmode;                // is quickmode active now?
bool lhlrmode;                  // is hlr active now?
int button_active;
Vec2<int> center_yx;            // half of dimensions
float tzs1, tzs2;
float tzp1, tzp2;
float tclip1, tclip2;
float trclip1, trclip2;
int frame = 0;                  // current frame number
Frame tcur;                     // current transform: object -> viewing
Point conor;                    // point to use in computing normal culling
bool want_plot;                 // user wants postscript plot
string psfile;
unique_ptr<Postscript> postscript; // currently drawing postscript
HiddenLineRemoval hlr;
bool dbuffer;
float thicksharp = 5.f;         // for postscript output (0.f=none, 1.f=normal)
float thicknormal = 1.f;

struct coord {
    void init(const Point& ppp) { frame = -1; count = 0; p = ppp; }
    int count;                  // num of references to this
    int frame;                  // frame number it was last transformed
    int ccode;                  // clipping plane condition codes
    Point p;                    // point in object frame
    Point pt;                   // point tranformed to view frame
    Point pp;                   // point projected onto screen
};

struct segment {
    mutable int frame;          // frame number it was last drawn
    coord* c1;
    coord* c2;
};

struct hash_segment {
    size_t operator()(const segment& s) const {
        return reinterpret_cast<uintptr_t>(s.c1)+reinterpret_cast<uintptr_t>(s.c2)*761;
    }
};

struct equal_segment {
    bool operator()(const segment& s1, const segment& s2) const { return s1.c1==s2.c1 && s1.c2==s2.c2; }
};

struct Node : noncopyable {
    virtual ~Node()                             { }
    enum class EType { polygon, line, linenor, point, pointnor };
    EType _type; // faster than virtual function or dynamic_cast() or hh::dynamic_exact_cast(), and avoids RTTI
 protected:
    Node(EType type)                            : _type(type) { }
};

struct NodePolygon : Node {
    NodePolygon()                               : Node(Node::EType::polygon) { }
    coord* repc;
    Vector pnor;
    PArray<segment*,4> ars;
};

struct NodeLine : Node {
    NodeLine(segment* ps, EType type = EType::line) : Node(type), s(ps) { }
    segment* s;
};

struct NodeLineNor : NodeLine {
    NodeLineNor(segment* ps, const Vector& n1, const Vector& n2) : NodeLine(ps, EType::linenor), nor(n1, n2) { }
    Vec2<Vector> nor;           // normal at each vertex
};

struct NodePoint : Node {
    NodePoint(coord* pp, EType type = EType::point) : Node(type), p(pp) { }
    coord* p;
};

struct NodePointNor : NodePoint {
    NodePointNor(coord* pp, Vector n)           : NodePoint(pp, EType::pointnor), nor(std::move(n)) { }
    Vector nor;
};

HH_SAC_ALLOCATE_FUNC(Mesh::MVertex, coord, v_coord);

struct hbfaceinfo {
    Point p;
    Vector v;
};
HH_SAC_ALLOCATE_FUNC(Mesh::MFace, hbfaceinfo, f_info);

class GXobject {
 public:
    ~GXobject()                                 { assertx(!_opened); }
    void open(bool todraw);
    void add(const A3dElem& el);
    void close();
    CArrayView<unique_ptr<Node>> traverse() const { assertx(!_opened); return _arn; }
    GMesh* mesh {nullptr};
 private:
    bool _opened {false};
    Array<unique_ptr<Node>> _arn;
    HashPoint _hp;                // Point -> hp_index
    Array<unique_ptr<coord>> _ac; // hp_index -> coord*  (not Array<coord> as resizing would invalidate pointers)
    Set<segment, hash_segment, equal_segment> _hs;
    Array<unique_ptr<Node>> _arnc; // list of current elements being added
    static bool s_idraw;           // draw elements as they are added
    static int s_nuvertices;
    static int s_nusegments;
    coord* add_coord(const Point& p); // search _hp+_ac for coord*
    segment* add_segment(coord* c1, coord* c2);
    void append(unique_ptr<Node> n);
};

class GXobjects {
 public:
    GXobjects();
    Vec<Frame,k_max_object> t;
    Vec<bool,k_max_object> vis;
    Vec<bool,k_max_object> cullface;
    Vec<int,k_max_object> culledge;
    Vec<bool,k_max_object> reverse_cull;
    Vec<bool,k_max_object> highlight_vertices;
    Vec<bool,k_max_object> show_sharp;
    int min_segn() const { return _imin; }
    int max_segn() const { return _imax; }
    void clear(int segn);
    void open(int segn);
    void add(const A3dElem& el);
    void close();
    void make_link(int oldsegn, int newsegn);
    int defined(int segn) const;
    GXobject& operator[](int i);
 private:
    int _imin {k_max_object};
    int _imax {0};
    // if _link[i], is a link to GXobject _link[i]
    Vec<int,k_max_object> _link;
    Array<unique_ptr<GXobject>> _ob;
    int _segn {-1};
    bool _idraw;
    GXobject* obp(int i) const;
};

bool GXobject::s_idraw;
int GXobject::s_nuvertices;
int GXobject::s_nusegments;

GXobjects g_xobs;

// *** HW callback functions

bool DerivedHW::key_press(string s) {
    return fkeyp(s);
}

void DerivedHW::button_press(int butnum, bool pressed, const Vec2<int>& yx) {
    Vec2<float> yxf = convert<float>(yx)/convert<float>(win_dims);
    bool shift = get_key_modifier(EModifier::shift);
    fbutp(butnum, pressed, shift, yxf);
    if (pressed) {
        if (butnum<=3) button_active = butnum;
    } else {
        if (butquick || buthlr) hw.redraw_now();
        button_active = 0;
    }
}

void DerivedHW::wheel_turn(float v) {
    fwheel(v);
}

void DerivedHW::draw_window(const Vec2<int>& dims) {
    is_window = true;
    win_dims = dims;
    center_yx = dims/2;
    fdraw();
}

void adjust_viewing() {
    float a = 1.f/min(win_dims);
    tzs1 = -.5f/(zoom*a);
    tzs2 = -.5f/(zoom*a);
    tzp1 = .5f/(zoom*win_dims[1]*a);
    tzp2 = .5f/(zoom*win_dims[0]*a);
    const float pclipsegments = 1.001f;
    tclip1 = zoom*win_dims[1]*a/pclipsegments;
    tclip2 = zoom*win_dims[0]*a/pclipsegments;
    trclip1 = 1.0001f/tclip1;
    trclip2 = 1.0001f/tclip2;
}

bool setup_ob(int i) {
    tcur = g_xobs.t[i]*tcami;
    Frame tinv; if (!assertw(invert(g_xobs.t[i], tinv))) return false;
    conor = tcam.p()*tinv;
    cullface = g_xobs.cullface[i];
    culledge = g_xobs.culledge[i];
    reverse_cull = g_xobs.reverse_cull[i];
    highlight_vertices = g_xobs.highlight_vertices[i];
    show_sharp = g_xobs.show_sharp[i];
    return true;
}

Point point_to_hlr_point(const Point& p) {
    return Point(p[0], .5f+p[1]*tzp1, .5f+p[2]*tzp2);
}

Point coord_to_hlr_point(const coord* c) {
    return point_to_hlr_point(c->pp);
}

void transf2(coord* c) {
    const Point& pt = c->pt;
    float pt0 = pt[0];
    int ccode = pt0<hither ? CCH : is_yonder && pt0>yonder ? CCY : CCN;
    if (!ccode) {
        float a = 1.f/pt0, pp1, pp2;
        c->pp[0] = -a;
        c->pp[1] = pp1 = pt[1]*a;
        c->pp[2] = pp2 = pt[2]*a;
        if (pp1<0) {
            if (pp1<-tclip1) ccode += CCR;
        } else {
            if (pp1> tclip1) ccode += CCL;
        }
        if (pp2<0) {
            if (pp2<-tclip2) ccode += CCD;
        } else {
            if (pp2> tclip2) ccode += CCU;
        }
    }
    c->ccode = ccode;
}

void transf(coord* c) {
    if (c->frame!=frame) {
        c->frame = frame;
        c->pt = c->p*tcur;
        transf2(c);
    }
}

void inbound(coord* c, const coord* c2) {
    int cc = c->ccode;
    float border;
    if (cc&CCH) {
        border = hither;
    } else if (cc&CCY) {
        border = yonder;
    } else assertnever("");
    float ratio = (border-c->pt[0])/(c2->pt[0]-c->pt[0]);
    c->pt[0] = border;
    c->pt[1] = c->pt[1]+(c2->pt[1]-c->pt[1])*ratio;
    c->pt[2] = c->pt[2]+(c2->pt[2]-c->pt[2])*ratio;
    transf2(c);
    assertx(!(c->ccode & (CCH | CCY)));
}

bool clip_side(coord* c1, const coord* c2, int axis, float val) {
    float s1 = c1->pt[0]+val*c1->pt[axis];
    float s2 = c2->pt[0]+val*c2->pt[axis];
    if ((s1<=0 && s2<=0) || (s1>=0 && s2>=0)) {
        return true;            // numerical problem
    } else {
        c1->pt = interp(c1->pt, c2->pt, s2/(s2-s1));
        transf2(c1);
        return false;
    }
}

bool clip2(coord* c, const coord* c2) {
    int cc = c->ccode, cc2 = c2->ccode;
    if (cc&CCR) {
        if (clip_side(c, c2, 1, +trclip1)) return true;
        cc = c->ccode;
    } else if (cc&CCL) {
        if (clip_side(c, c2, 1, -trclip1)) return true;
        cc = c->ccode;
    }
    if (cc&cc2) return true;
    if (cc&CCD) {
        if (clip_side(c, c2, 2, +trclip2)) return true;
        cc = c->ccode;
    } else if (cc&CCU) {
        if (clip_side(c, c2, 2, -trclip2)) return true;
        cc = c->ccode;
    }
    if (cc&cc2) return true;
    if (cc) return true;        // numerical problem
    return false;
}

inline bool is_cull(const Point& wp, const Vector& nor) {
    // float a = dot(wp-conor, nor);
    const Point& con = conor;
    float a = ((wp[0]-con[0])*nor[0] + (wp[1]-con[1])*nor[1] + (wp[2]-con[2])*nor[2]);
    return reverse_cull ? (a<0) : (a>0);
}

void draw_point(coord* c) {
    transf(c);
    if (c->ccode) return;
    if (lhlrmode && !hlr.draw_point(coord_to_hlr_point(c))) return;
    float c1s0 = center_yx[1]+c->pp[1]*tzs1; // no need for +.5f because center_yx already centered
    float c1s1 = center_yx[0]+c->pp[2]*tzs2;
    hw.draw_point(V(c1s1, c1s0));
    if (highlight_vertices) {
        hw.draw_point(V(c1s1-1.f, c1s0-1.f));
        hw.draw_point(V(c1s1+1.f, c1s0-1.f));
        hw.draw_point(V(c1s1-1.f, c1s0+1.f));
        hw.draw_point(V(c1s1+1.f, c1s0+1.f));
    }
    if (postscript) postscript->point(.5f-tzp1*c->pp[1], .5f+tzp2*c->pp[2]);
}

void draw_segment(coord* c1, coord* c2); // forward declaration

void draw_fisheye(const coord* c1, const coord* c2) {
    assertx(quicki>0);
    Point p1 = c1->pt, p2 = c2->pt;
    Vector vd = (p2-p1)/float(quicki);
    coord o1, o2;
    for_int(i, quicki+1) {
        Vector v = p1-Point(0.f, 0.f, 0.f);
        assertw(v.normalize());
        v[0] = 1.f;
        v *= 1.0001f*hither/v[0];
        o2.pt = Point(0.f, 0.f, 0.f)+v;
        transf2(&o2);
        assertx(!(o2.ccode&(CCH | CCY)));
        if (i) draw_segment(&o1, &o2);
        o1 = o2;
        p1 += vd;
    }
}

void slow_draw_seg(coord* c1, coord* c2) {
    int cc1 = c1->ccode;
    int cc2 = c2->ccode;
    coord ct1, ct2;
    if (cc1 || cc2) {
        if (cc1&cc2) return;
        if (cc1) {
            ct1 = *c1; c1 = &ct1;
            if (cc1 & (CCH | CCY)) { inbound(c1, c2); cc1 = c1->ccode; }
        }
        if (cc2) {
            ct2 = *c2; c2 = &ct2;
            if (cc2 & (CCH | CCY)) { inbound(c2, c1); cc2 = c2->ccode; }
        }
        assertx(!((cc1 | cc2) & (CCH | CCY)));
        if (cc1&cc2) return;
        if (!fisheye) {
            if (cc1 && clip2(c1, c2)) return;
            if (cc2 && clip2(c2, c1)) return;
        }
    }
    if (lhlrmode) {
        hlr.draw_segment(coord_to_hlr_point(c1), coord_to_hlr_point(c2));
        return;
    }
    if (fisheye) { fisheye = false; draw_fisheye(c1, c2); fisheye = true; return; }
    float c1s0 = center_yx[1]+c1->pp[1]*tzs1;
    float c1s1 = center_yx[0]+c1->pp[2]*tzs2;
    float c2s0 = center_yx[1]+c2->pp[1]*tzs1;
    float c2s1 = center_yx[0]+c2->pp[2]*tzs2;
    hw.draw_segment(V(c1s1, c1s0), V(c2s1, c2s0));
    if (highlight_vertices) {
        hw.draw_segment(V(c1s1-1.f, c1s0-1.f), V(c1s1+1.f, c1s0+1.f));
        hw.draw_segment(V(c1s1+1.f, c1s0-1.f), V(c1s1-1.f, c1s0+1.f));
        hw.draw_segment(V(c2s1-1.f, c2s0-1.f), V(c2s1+1.f, c2s0+1.f));
        hw.draw_segment(V(c2s1+1.f, c2s0-1.f), V(c2s1-1.f, c2s0+1.f));
    }
    if (postscript) postscript->line(.5f-tzp1*c1->pp[1], .5f+tzp2*c1->pp[2], .5f-tzp1*c2->pp[1], .5f+tzp2*c2->pp[2]);
}

void fast_draw_seg(coord* c1, coord* c2) {
    int cc1 = c1->ccode;
    int cc2 = c2->ccode;
    coord ct1, ct2;
    if (cc1 || cc2) {
        if (cc1&cc2) return;
        if (cc1) {
            ct1 = *c1; c1 = &ct1;
            if (cc1 & (CCH | CCY)) { inbound(c1, c2); cc1 = c1->ccode; }
        }
        if (cc2) {
            ct2 = *c2; c2 = &ct2;
            if (cc2 & (CCH | CCY)) { inbound(c2, c1); cc2 = c2->ccode; }
        }
        assertx(!((cc1 | cc2) & (CCH | CCY)));
        if (cc1&cc2) return;
        if (cc1 && clip2(c1, c2)) return;
        if (cc2 && clip2(c2, c1)) return;
    }
    float c1s0 = center_yx[1]+c1->pp[1]*tzs1; // no need for +.5f because center_yx is correctly centered
    float c1s1 = center_yx[0]+c1->pp[2]*tzs2;
    float c2s0 = center_yx[1]+c2->pp[1]*tzs1;
    float c2s1 = center_yx[0]+c2->pp[2]*tzs2;
    hw.draw_segment(V(c1s1, c1s0), V(c2s1, c2s0));
}

void draw_segment(coord* c1, coord* c2) {
    if (fisheye || lhlrmode || highlight_vertices || postscript)
        slow_draw_seg(c1, c2);
    else
        fast_draw_seg(c1, c2);
}

void draw_node(const Node* un) {
    switch (un->_type) {
     bcase Node::EType::polygon: {
         auto n = down_cast<const NodePolygon*>(un);
         if (!(cullface && is_cull(n->repc->p, n->pnor))) {
             for (segment* s : n->ars) {
                 if (s->frame==frame) continue;
                 s->frame = frame;
                 transf(s->c1); transf(s->c2);
                 draw_segment(s->c1, s->c2);
             }
         }
     }
     bcase Node::EType::linenor: {
         auto n = down_cast<const NodeLineNor*>(un);
         segment* s = n->s;
         if (s->frame!=frame &&
             !((culledge==1 && (is_cull(s->c1->p, n->nor[0]) && is_cull(s->c2->p, n->nor[1]))) ||
               (culledge==2 && (is_cull(s->c1->p, n->nor[0]) || is_cull(s->c2->p, n->nor[1]))))) {
             s->frame = frame;
             transf(s->c1); transf(s->c2);
             draw_segment(s->c1, s->c2);
         }
     }
     bcase Node::EType::line: {
         auto n = down_cast<const NodeLine*>(un);
         segment* s = n->s;
         if (s->frame!=frame) {
             s->frame = frame;
             transf(s->c1); transf(s->c2);
             draw_segment(s->c1, s->c2);
         }
     }
     bcase Node::EType::pointnor: {
         auto n = down_cast<const NodePointNor*>(un);
         if (!is_cull(n->p->p, n->nor))
             draw_point(n->p);
     }
     bcase Node::EType::point: {
         auto n = down_cast<const NodePoint*>(un);
         draw_point(n->p);
     }
     bdefault: assertnever("");
    }
}

void draw_list(CArrayView<unique_ptr<Node>> arn) {
    for_int(i, arn.num()) {
        if (i%interval_check_stop==0 && !postscript && hw.suggests_stop()) break;
        if (lquickmode && i%quicki!=0) continue;
        const Node* n = arn[i].get();
        draw_node(n);
    }
}

void enter_hidden_polygon(Polygon& poly, int andcodes, int orcodes) {
    // If completely on one side of a clipping plane, reject
    if (andcodes) return;
    // If not all vertices are beyond hither, do clipping
    // don't have to worry at all about yonder plane!
    if (orcodes&CCH) {
        float s = 1.95f;
        // s = 2.2; // debug, shrink inside screen boundaries
        poly.intersect_hyperplane(Point(hither, 0.f, 0.f), Vector(+1.f, 0.f, 0.f));
        if (!poly.num()) return;
        poly.intersect_hyperplane(Point(0.f, 0.f, 0.f), Vector(1.f, +s*tzp1, 0.f));
        if (!poly.num()) return;
        poly.intersect_hyperplane(Point(0.f, 0.f, 0.f), Vector(1.f, -s*tzp1, 0.f));
        if (!poly.num()) return;
        poly.intersect_hyperplane(Point(0.f, 0.f, 0.f), Vector(1.f, 0.f, +s*tzp2));
        if (!poly.num()) return;
        poly.intersect_hyperplane(Point(0.f, 0.f, 0.f), Vector(1.f, 0.f, -s*tzp2));
        if (!poly.num()) return;
    }
    for (auto& p : poly) {
        if (!assertw(p[0]>0)) return;
        float a = 1.f/p[0];
        p[0] = -a;
        p[1] = p[1]*a;
        p[2] = p[2]*a;
        p = point_to_hlr_point(p);
    }
    // Note: polygons that are within hither/yonder are not clipped to
    // sides of screen -> so they may extend outside (0..1) range.
    // But, polygons completely off screen are culled.
    hlr.enter(poly);
}

void enter_hidden_polygons(CArrayView<unique_ptr<Node>> arn) {
    Polygon poly;
    for_int(i, arn.num()) {
        if (i%interval_check_stop==0 && !postscript && hw.suggests_stop()) break;
        Node* un = arn[i].get();
        if (un->_type==Node::EType::polygon) {
            auto n = down_cast<NodePolygon*>(un);
            if (cullface && is_cull(n->repc->p, n->pnor)) continue;
            poly.init(0);
            int andcodes = CCH | CCY | CCR | CCL | CCD | CCU, orcodes = 0;
            {
                coord* cfirst; {
                    segment* s1 = n->ars[0];
                    segment* s2 = n->ars[1];
                    cfirst = (s1->c1==s2->c1||s1->c1==s2->c2) ? s1->c2 : s1->c1;
                }
                coord* cc = cfirst;
                for (segment* s : n->ars) {
                    cc = s->c1==cc ? s->c2 : s->c1;
                    transf(cc);
                    andcodes &= cc->ccode;
                    orcodes |= cc->ccode;
                    poly.push(cc->pt); // viewing frame coordinates
                }
                if (!assertw(cc==cfirst)) return;
            }
            enter_hidden_polygon(poly, andcodes, orcodes);
        }
    }
}

void mesh_init(GMesh& mesh) {
    if (mesh.gflags().flag(g3d::mflag_ok).set(true)) return;
    Set<Face> fredo;
    for (Vertex v : mesh.vertices()) {
        if (mesh.flags(v).flag(g3d::vflag_ok).set(true)) continue;
        v_coord(v).init(mesh.point(v));
        for (Face f : mesh.faces(v)) { fredo.add(f); }
    }
    for (Face f : mesh.faces()) {
        if (!mesh.flags(f).flag(g3d::fflag_ok)) fredo.add(f);
    }
    Polygon poly;
    for (Face f : fredo) {
        mesh.flags(f).flag(g3d::fflag_ok) = true;
        mesh.polygon(f, poly);
        f_info(f).p = poly[0];
        f_info(f).v = poly.get_normal_dir();
    }
}

void mesh_transform(GMesh& mesh) {
    for (Vertex v : mesh.vertices()) {
        coord& cc = v_coord(v);
        cc.frame = -1;          // force transformation
        transf(&cc);
    }
}

void mesh_visibility(GMesh& mesh) {
    for (Face f : mesh.faces()) {
        mesh.flags(f).flag(fflag_invisible) = cullface && is_cull(f_info(f).p, f_info(f).v);
    }
}

void enter_mesh_hidden_polygons(GMesh& mesh) {
    mesh_init(mesh);
    mesh_transform(mesh);
    mesh_visibility(mesh);
    int i = 0;
    Polygon poly;
    for (Face f : mesh.faces()) {
        if (i++%interval_check_stop==0 && !postscript && hw.suggests_stop()) break;
        if (cullface && mesh.flags(f).flag(fflag_invisible)) continue;
        poly.init(0);
        int andcodes = CCH | CCY | CCR | CCL | CCD | CCU, orcodes = 0;
        for (Vertex v : mesh.vertices(f)) {
            coord& cc = v_coord(v);
            andcodes &= cc.ccode;
            orcodes |= cc.ccode;
            poly.push(cc.pt);   // viewing frame coordinates
        }
        enter_hidden_polygon(poly, andcodes, orcodes);
    }
}

void draw_mesh(GMesh& mesh) {
    mesh_init(mesh);
    mesh_transform(mesh);
    mesh_visibility(mesh);
    int i = 0;
    if (!postscript && !show_sharp && !lquickmode && thicksharp && thicknormal) {
        for (Edge e : mesh.edges()) {
            if (i++%interval_check_stop==0 && hw.suggests_stop()) break;
            Face f2 = mesh.face2(e);
            if (cullface && mesh.flags(mesh.face1(e)).flag(fflag_invisible) &&
                (!f2 || mesh.flags(f2).flag(fflag_invisible)))
                continue;
            draw_segment(&v_coord(mesh.vertex1(e)), &v_coord(mesh.vertex2(e)));
        }
    } else {
        if (postscript) postscript->edge_width(1.f);
        for (Edge e : mesh.edges()) {
            if (i++%interval_check_stop==0 && !postscript && hw.suggests_stop()) break;
            if (lquickmode && i%quicki!=0) continue;
            Face f2 = mesh.face2(e);
            if (cullface && mesh.flags(mesh.face1(e)).flag(fflag_invisible) &&
                (!f2 || mesh.flags(f2).flag(fflag_invisible)))
                continue;
            bool is_sharp = mesh.flags(e).flag(GMesh::eflag_sharp) || !f2;
            float curthick = is_sharp ? thicksharp : thicknormal;
            if (!curthick) continue;
            if (show_sharp && !is_sharp) continue;
            if (postscript) {
                postscript->edge_width(curthick);
                if (silhouette && f2 &&
                    !mesh.flags(mesh.face1(e)).flag(fflag_invisible) &&
                    !mesh.flags(mesh.face2(e)).flag(fflag_invisible)) continue;
            }
            draw_segment(&v_coord(mesh.vertex1(e)), &v_coord(mesh.vertex2(e)));
        }
        if (postscript) postscript->edge_width(1.f);
    }
}

void hlr_draw_seg(const Point& p1, const Point& p2) {
    Vec2<Point> pa { p1, p2 };
    Vec2<coord> co;
    for_int(i, 2) {
        co[i].ccode = 0;
        co[i].pp = Point(pa[i][0], (pa[i][1]-.5f)/tzp1, (pa[i][2]-.5f)/tzp2);
        // faked this part to get fisheye to work
        float a = hither*1.001f;
        co[i].pt = Point(a, co[i].pp[1]*a, co[i].pp[2]*a);
    }
    lhlrmode = false;
    draw_segment(&co[0], &co[1]);
    lhlrmode = true;
}

void draw_all() {
    lquickmode = quickmode || (butquick && button_active);
    lhlrmode = hlrmode || (buthlr && !button_active);
    if (lhlrmode) {
        int oldframe = frame;
        hlr.clear(); hlr.set_draw_seg_cb(hlr_draw_seg);
        for (int i = g_xobs.min_segn(); i<=g_xobs.max_segn(); i++) {
            if (!postscript && hw.suggests_stop()) break;
            if (!g_xobs.defined(i) || !g_xobs.vis[i]) continue;
            if (!setup_ob(i)) continue;
            frame++;
            enter_hidden_polygons(g_xobs[i].traverse());
            if (g_xobs[i].mesh) enter_mesh_hidden_polygons(*g_xobs[i].mesh);
        }
        frame = oldframe;
    }
    for (int i = g_xobs.min_segn(); i<=g_xobs.max_segn(); i++) {
        if (!postscript && hw.suggests_stop()) break;
        if (!g_xobs.defined(i) || !g_xobs.vis[i]) continue;
        if (!setup_ob(i)) continue;
        frame++;
        draw_list(g_xobs[i].traverse());
        if (g_xobs[i].mesh) draw_mesh(*g_xobs[i].mesh);
        if (postscript) postscript->flush_write("% EndG3dObject\n");
    }
    if (lhlrmode) {
        lhlrmode = false;
        hlr.clear();
    }
}

void toggle_attribute(Vec<bool,k_max_object>& attrib) {
    bool allvis = true;
    for (int i = g_xobs.min_segn(); i<=g_xobs.max_segn(); i++) {
        if (g_xobs.defined(i) && !g_xobs.vis[i]) allvis = false;
    }
    for_int(i, k_max_object) {
        if (!allvis && (!g_xobs.defined(i) || !g_xobs.vis[i])) continue;
        bool& val = attrib[i];
        val = !val;
    }
}

void toggle_attribute(Vec<int,k_max_object>& attrib, int numstates) {
    bool allvis = true;
    for (int i = g_xobs.min_segn(); i<=g_xobs.max_segn(); i++) {
        if (g_xobs.defined(i) && !g_xobs.vis[i]) allvis = false;
    }
    for_int(i, k_max_object) {
        if (!allvis && (!g_xobs.defined(i) || !g_xobs.vis[i])) continue;
        int& val = attrib[i];
        val = (val+1)%numstates;
    }
}

// *** GXobject

void GXobject::open(bool todraw) {
    assertx(!_opened); _opened = true;
    s_idraw = todraw;
    s_nuvertices = s_nusegments = 0;
}

void GXobject::add(const A3dElem& el) {
    assertx(_opened);
    assertx(el.num());
    switch (el.type()) {
     bcase A3dElem::EType::polygon: {
         auto n = make_unique<NodePolygon>();
         n->pnor = el.pnormal(); assertw(!is_zero(n->pnor));
         coord* cl = add_coord(el[el.num()-1].p);
         for_int(i, el.num()) {
             coord* cc = add_coord(el[i].p);
             n->ars.push(add_segment(cl, cc));
             cl = cc;
         }
         n->repc = cl;           // pick an arbitrary vertex for now
         append(std::move(n));
     }
     bcase A3dElem::EType::point: {
         if (is_zero(el[0].n))
             append(make_unique<NodePoint>(add_coord(el[0].p)));
         else
             append(make_unique<NodePointNor>(add_coord(el[0].p), el[0].n));
     }
     bcase A3dElem::EType::polyline: {
         coord* cl = add_coord(el[0].p);
         for_intL(i, 1, el.num()) {
             coord* cc = add_coord(el[i].p);
             if (is_zero(el[i-1].n) && is_zero(el[i].n))
                 append(make_unique<NodeLine>(add_segment(cl, cc)));
             else
                 append(make_unique<NodeLineNor>(add_segment(cl, cc), el[i-1].n, el[i].n));
             cl = cc;
         }
     }
     bdefault: assertnever("");
    }
}

coord* GXobject::add_coord(const Point& p) {
    if (!nohash) {
        int vi = _hp.enter(p);
        if (vi<_ac.num()) return _ac[vi].get();
    }
    s_nuvertices++;
    _ac.push(make_unique<coord>());
    coord* c = _ac.last().get();
    c->init(p);
    return c;
}

segment* GXobject::add_segment(coord* c1, coord* c2) {
    segment s;
    s.c1 = c1<c2 ? c1 : c2;
    s.c2 = c1<c2 ? c2 : c1;
    s.frame = -1;
    bool is_new; const segment& sret = _hs.enter(s, is_new);
    if (is_new) s_nusegments++;
    return const_cast<segment*>(&sret);
}

void GXobject::append(unique_ptr<Node> n) {
    _arnc.push(std::move(n));
    if (s_idraw) {
        assertx(is_window);
        draw_node(_arnc.last().get());
    }
}

void GXobject::close() {
    assertx(_opened); _opened = false;
    if (datastat) SHOW(s_nuvertices, s_nusegments);
    // assign repc vertices
    for_int(i, _arnc.num()) {
        Node* un = _arnc[i].get();
        if (un->_type==Node::EType::polygon) {
            auto n = down_cast<NodePolygon*>(un);
            for (segment* s : n->ars) { s->c1->count++; s->c2->count++; }
        }
    }
    for_int(i, _arnc.num()) {
        Node* un = _arnc[i].get();
        if (un->_type==Node::EType::polygon) {
            auto n = down_cast<NodePolygon*>(un);
            coord* maxc = nullptr;
            int maxn = -1;
            for (segment* s : n->ars) {
                if (s->c1->count>maxn) { maxn = s->c1->count; maxc = s->c1; }
                if (s->c2->count>maxn) { maxn = s->c2->count; maxc = s->c2; }
            }
            n->repc = assertx(maxc);
            for (segment* s : n->ars) { s->c1->count -= 1; s->c2->count -= 1; }
            maxc->count = 100000;
        }
    }
    _arn.push_array(std::move(_arnc));
}

// *** GXobjects

GXobjects::GXobjects() : _ob(k_max_object) {
    for_int(i, k_max_object) {
        _link[i] = 0;
        t[i] = Frame::identity();
        vis[i] = false;
        cullface[i] = true;
        culledge[i] = 0;
        reverse_cull[i] = false;
        highlight_vertices[i] = false;
        show_sharp[i] = false;
    }
}

void GXobjects::clear(int segn) {
    assertx(_segn==-1); assertx(_link.ok(segn));
    _link[segn] = 0;
    _ob[segn] = nullptr;
    // I could update _imin, _imax here
}

void GXobjects::open(int segn) {
    assertx(_segn==-1);
    _segn = segn;
    assertx(_link.ok(segn));
    _link[_segn] = 0;
    if (!_ob[_segn]) {          // create GXobject
        _imin = min(_imin, _segn);
        _imax = max(_imax, _segn);
        _ob[_segn] = make_unique<GXobject>();
    }
    _idraw = is_window && setup_ob(_segn);
    if (_idraw) hw.begin_draw_visible();
    _ob[_segn]->open(_idraw);
}

void GXobjects::add(const A3dElem& el) {
    assertx(_segn!=-1);
    _ob[_segn]->add(el);
}

void GXobjects::close() {
    assertx(_segn!=-1);
    _ob[_segn]->close();
    if (_idraw)
        hw.end_draw_visible();
    _segn = -1;
}

void GXobjects::make_link(int oldsegn, int newsegn) {
    assertx(_segn==-1); assertx(_link.ok(oldsegn)); assertx(_link.ok(newsegn)); assertx(oldsegn!=newsegn);
    clear(newsegn);
    _link[newsegn] = oldsegn;
    _imin = min(_imin, newsegn);
    _imax = max(_imax, newsegn);
}

GXobject* GXobjects::obp(int i) const {
    assertx(_link.ok(i));
    if (_ob[i]) return _ob[i].get();
    if (_link[i]) return _ob[_link[i]].get();
    return nullptr;
}

// recursion on links is not permitted, only simple link allowed
int GXobjects::defined(int i) const {
    return obp(i) ? 1 : 0;
}

GXobject& GXobjects::operator[](int i) {
    return *assertx(obp(i));
}

} // namespace

// *** HB function calls

bool HB::init(Array<string>& aargs,
              bool (*pfkeyp)(const string& s),
              void (*pfbutp)(int butnum, bool pressed, bool shift, const Vec2<float>& yx),
              void (*pfwheel)(float v),
              void (*pfdraw)()) {
    fkeyp = assertx(pfkeyp); fbutp = assertx(pfbutp); fwheel = assertx(pfwheel); fdraw = assertx(pfdraw);
    psfile = "g3d.ps";
    quicki = 4;
    hither = k_default_hither;
    bool hw_success = hw.init(aargs);
    ParseArgs args(aargs, "HB_X");
    ARGSF(datastat,                             ": geometric hashing stats");
    ARGSP(psfile,                               "file.ps : set postscript output");
    ARGSF(nohash,                               ": turn off vertex hashing");
    args.p("-thicks[harp]", thicksharp,         "f : width of sharp edges");
    args.p("-thickn[ormal]", thicknormal,       "f : width of edges");
    ARGSF(silhouette,                           ": in hidden-line, draw only silh.");
    args.other_args_ok(); args.other_options_ok(); args.disallow_prefixes();
    if (!args.parse_and_extract(aargs) || !hw_success) return false;
    return true;
}

void HB::set_window_title(string s) {
    hw.set_window_title(std::move(s));
}

void HB::open() {
    hw.set_default_geometry(k_default_geometry);
    dbuffer = true;
    hw.set_double_buffering(dbuffer);
    hw.open();
}

void HB::watch_fd0(void (*pfinpu)()) {
    hw._finpu = pfinpu;
    hw.watch_fd0(!!hw._finpu);
}

void HB::quit() {
    hw.quit();
}

void HB::redraw_later() {
    hw.redraw_later();
}

void HB::redraw_now() {
    hw.redraw_now();
}

Vec2<int> HB::get_extents() {
    return win_dims;
}

bool HB::get_pointer(Vec2<float>& yxf) {
    Vec2<int> yx; if (!hw.get_pointer(yx)) return false;
    yxf = convert<float>(yx)/convert<float>(win_dims);
    return true;
}

void HB::set_camera(const Frame& p_real_t, float p_real_zoom, const Frame& p_view_t, float p_view_zoom) {
    dummy_use(p_real_t, p_real_zoom);
    tcam = p_view_t;
    tcami = inverse(tcam);
    zoom = p_view_zoom;
}

float HB::get_hither() {
    return hither;
}

float HB::get_yonder() {
    return is_yonder ? yonder : 0.f;
}

void HB::set_hither(float h) {
    hither = h;
    if (!hither) hither = k_default_hither;
}

void HB::set_yonder(float y) {
    yonder = y;
    is_yonder = yonder!=0.f;
}

void HB::set_current_object(int) {
    // no lighting feature in g3dX
}

void HB::update_seg(int segn, const Frame& f, bool vis) {
    assertx(segn>=0 && segn<k_max_object);
    g_xobs.t[segn] = f;
    g_xobs.vis[segn] = vis;
}

void HB::draw_space() {
    hw.clear_window();
    if (is_yonder && yonder<hither) yonder = hither*1.001f;
    adjust_viewing();
    unique_ptr<WFile> pwf;
    if (want_plot) {
        want_plot = false;
        SHOW("starting plot...");
        pwf = make_unique<WFile>(psfile);
        postscript = make_unique<Postscript>((*pwf)(), win_dims[1], win_dims[0]);
    }
    draw_all();
    if (postscript) {
        postscript = nullptr;
        pwf = nullptr;
        SHOW("...plot finished");
    }
}

bool HB::special_keypress(char ch) {
    switch (ch) {
     bcase 'b':
        toggle_attribute(g_xobs.cullface);
        hw.redraw_now();
     bcase 'l':
        toggle_attribute(g_xobs.culledge, 3);
        hw.redraw_now();
     bcase 'r':
        toggle_attribute(g_xobs.reverse_cull);
        hw.redraw_now();
     bcase 'v':
        toggle_attribute(g_xobs.highlight_vertices);
        hw.redraw_now();
     bcase 'e':
        toggle_attribute(g_xobs.show_sharp);
        hw.redraw_now();
     bcase 'f':
        fisheye = !fisheye;
        hw.redraw_now();
     bcase 'P':
        want_plot = true;
        hw.redraw_now();
     bcase 'Q':
        butquick = !butquick; quickmode = false; buthlr = false;
        hw.redraw_now();
     bcase 'H':
        buthlr = !buthlr; butquick = false;
        hw.redraw_now();
     bcase 'q':
        quickmode = !quickmode; butquick = false;
        hw.redraw_now();
     bcase ']':
        quicki *= 2;
        if (quickmode || butquick || fisheye) hw.redraw_now();
     bcase '[':
        quicki /= 2;
        if (!quicki) quicki = 1;
        if (quickmode || butquick || fisheye) hw.redraw_now();
     bcase 'd':
        hw.set_double_buffering(dbuffer = !dbuffer);
     bcase 'h':
        hlrmode = !hlrmode;
        hw.redraw_now();
     bcase '/': {
         string s = g3d::statefile;
         if (hw.query(V(30, 2), "Stateg3d:", s)) g3d::statefile = s;
     }
     bcase '\r':                // <enter>/<ret> key (== uchar{13} == 'M'-64)
     ocase '\n':                // G3d -key $'\n'
        hw.make_fullscreen(!hw.is_fullscreen());
     bcase '?': {
         const string s = 1+R"(
Device commands:
  Per object:
<b>ackfacecull  back<l>inecull  <r>eversecull
highlight<v>ertices  show_sharp<e>dges
  Global:
<f>isheyelens
<h>lr_mode  button<H>lr  <q>uickmode  button<Q>uick  <[>, <]>:change_quicki
<P>sg3d  <d>oublebuffer
</>setstatefile  <cntrl-C>quit
)";
         std::cerr << s;
     }
     bdefault:
        return false;
    }
    return true;
}

string HB::show_info() {
    return sform("[X %c%c%c%c%c%c%c%c]",
                 cullface ? 'b' : ' ',
                 " lL"[culledge],
                 reverse_cull ? 'r' : ' ',
                 highlight_vertices ? 'v' : ' ',
                 show_sharp ? 'e' : ' ',
                 fisheye ? 'f' : ' ',
                 hlrmode ? 'h' : ' ',
                 quickmode ? 'q' : butquick ? 'Q' : buthlr ? 'H' : ' ');
}

bool HB::world_to_vdc(const Point& pi, float& xo, float& yo, float& zo) {
    Point p = pi*tcami;
    zo = p[0];
    if (p[0]<hither) return false;
    xo = .5f-p[1]/p[0]*tzp1;
    yo = .5f-p[2]/p[0]*tzp2;
    return true;
}

void HB::draw_segment(const Vec2<float>& yx1, const Vec2<float>& yx2) {
    hw.draw_segment(yx1*convert<float>(win_dims), yx2*convert<float>(win_dims));
}

Vec2<int> HB::get_font_dims() {
    return hw.get_font_dims();
}

void HB::draw_text(const Vec2<float>&yx, const string& s) {
    hw.draw_text(convert<int>(floor(yx*convert<float>(win_dims))), s);
}

void HB::draw_row_col_text(const Vec2<int>& yx, const string& s) {
    auto fdims = HB::get_font_dims();
    hw.draw_text(V((yx[0]<0 ? win_dims[0]+yx[0]*fdims[0] :
                    yx[0]*fdims[0]),
                   (yx[1]==INT_MAX ? (win_dims[1]-narrow_cast<int>(s.size())*fdims[1])/2 :
                    yx[1]<0       ? win_dims[1]+yx[1]*fdims[1]-2 :
                    yx[1]*fdims[1]+2)),
                 s);
}

void HB::clear_segment(int segn) {
    g_xobs.clear(segn);
}

void HB::open_segment(int segn) {
    g_xobs.open(segn);
}

void HB::segment_add_object(const A3dElem& el) {
    g_xobs.add(el);
}

void HB::close_segment() {
    g_xobs.close();
}

void HB::segment_attach_mesh(int segn, GMesh* pmesh) {
    g_xobs[segn].mesh = pmesh;
}

void HB::make_segment_link(int oldsegn, int newsegn) {
    g_xobs.make_link(oldsegn, newsegn);
}

void HB::segment_morph_mesh(int segn, float finterp) {
    dummy_use(segn, finterp);
}

void HB::reload_textures() {
}

void HB::flush() {
    hw.hard_flush();
}

void HB::beep() {
    hw.beep();
}

int HB::id() {
    return 1000;
}

void* HB::escape(void* code, void* data) {
    dummy_use(code, data);
    return nullptr;
}
