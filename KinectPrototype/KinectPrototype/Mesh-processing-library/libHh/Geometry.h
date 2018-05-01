// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_GEOMETRY_H_
#define MESH_PROCESSING_LIBHH_GEOMETRY_H_

#include "Vec.h"
#include "SGrid.h"

namespace hh {

class Frame; struct Point;

// My Vector, Point, Frame classes assumes row vectors (rather than column vectors).
// This is similar to RenderMan -- http://www.levork.org/2002/02/07/row-and-column-matrix-conventions-in-renderman/
// http://en.wikipedia.org/wiki/Row_vector
// https://www.opengl.org/discussion_boards/archive/index.php/t-153574.html :
//   OpenGL was long ago derived from SGI's propietary graphics library 'Irix GL'
//   In its docs, matrix ops were specified as operating on row vectors on the matrix left.

// *** Vector (lives in 3D linear space; represents translation rather than position).
struct Vector : Vec3<float> {
    Vector()                                    = default;
    constexpr Vector(float x, float y, float z) : Vec3<float>(x, y, z) { }
    constexpr Vector(Vec3<float> v)             : Vec3<float>(v) { }
    bool normalize();
};
Vector operator*(const Vector& v, const Frame& f);
Vector operator*(const Frame& f, const Vector& normal);
inline Vector& operator*=(Vector& v, const Frame& f)    { return v = v*f; }
constexpr Vector cross(const Vector& v1, const Vector& v2);
inline Vector normalized(Vector v)                      { assertx(v.normalize()); return v; }
inline Vector ok_normalized(Vector v)                   { v.normalize(); return v; }
// Overload to have lower precision than RangeOp.h templates (which return double).
// Must overload several versions to hide the influence of the template in RangeOp.h
// #define E static_cast<float>(double{v1[0]}*v2[0]+double{v1[1]}*v2[1]+double{v1[2]}*v2[2])
#define E v1[0]*v2[0]+v1[1]*v2[1]+v1[2]*v2[2]
inline constexpr float dot(const Vec3<float>& v1, const Vec3<float>& v2) { return E; }
inline constexpr float dot(const Vec3<float>& v1, const Vector& v2)      { return E; }
inline constexpr float dot(const Vector& v1, const Vec3<float>& v2)      { return E; }
inline constexpr float dot(const Vector& v1, const Vector& v2)           { return E; }
#undef E
inline constexpr float mag2(const Vec3<float>& v1)      { return dot(v1, v1); }
inline constexpr float mag2(const Vector& v1)           { return dot(v1, v1); }
inline float mag(const Vec3<float>& v1)                 { return sqrt(mag2(v1)); }
inline float mag(const Vector& v1)                      { return sqrt(mag2(v1)); }

// *** Point (lives in 3D affine space; represents position rather than translation).
struct Point : Vec3<float> {
    Point()                                     = default;
    constexpr Point(float x, float y, float z)  : Vec3<float>(x, y, z) { }
    constexpr Point(Vec3<float> p)              : Vec3<float>(p) { }
};
Point operator*(const Point& p, const Frame& f);
inline Point& operator*=(Point& p, const Frame& f)      { return p = p*f; }
inline Vector to_Vector(const Point& p)                 { return Vector(p[0], p[1], p[2]); }
inline Point to_Point(const Vector& v)                  { return Point(v[0], v[1], v[2]); }
inline float pvdot(const Point& p, const Vector& v)     { return dot(to_Vector(p), v); }
Vector cross(const Point& p1, const Point& p2, const Point& p3);
float area2(const Point& p1, const Point& p2, const Point& p3);
Point centroid(CArrayView<Point> pa);
#define E square(v1[0]-v2[0])+square(v1[1]-v2[1])+square(v1[2]-v2[2])
inline float dist2(const Vec3<float>& v1, const Vec3<float>& v2)        { return E; }
inline float dist2(const Vec3<float>& v1, const Point& v2)              { return E; }
inline float dist2(const Point& v1, const Vec3<float>& v2)              { return E; }
inline float dist2(const Point& v1, const Point& v2)                    { return E; }
#undef E
inline float dist(const Vec3<float>& v1, const Vec3<float>& v2)         { return sqrt(dist2(v1, v2)); }
inline float dist(const Vec3<float>& v1, const Point& v2)               { return sqrt(dist2(v1, v2)); }
inline float dist(const Point& v1, const Vec3<float>& v2)               { return sqrt(dist2(v1, v2)); }
inline float dist(const Point& v1, const Point& v2)                     { return sqrt(dist2(v1, v2)); }

inline bool is_unit(const Vec3<float>& v)       { return abs(mag2(v)-1.f)<1e-4f; }

// *** Frame: either coordinate frame or transformation frame.
//  Affine transformation from 3D to 3D.  (new_Point, 1.f) = (Point, 1.f) * Frame,
//    where (Point, 1.f) is *row* 4-vector.
//  Can be interpreted as 4x3 matrix where rows 0..2 are mappings of axes 0..2, and row 3 is mapping of origin.
//  Note that Vector*Frame correctly assumes a row 4-vector (Vector, 0.f).
class Frame : public SGrid<float, 4, 3> {
    using base = SGrid<float, 4, 3>;
 public:
    Frame()                                     = default;
    Frame(Vector v0, Vector v1, Vector v2, Point q) : base(V<Vec3<float>>(v0, v1, v2, q)) { }
    Vector& v(int i)                    { HH_CHECK_BOUNDS(i, 3); return static_cast<Vector&>((*this)[i]); }
    const Vector& v(int i) const        { HH_CHECK_BOUNDS(i, 3); return static_cast<const Vector&>((*this)[i]); }
    Point& p()                                  { return static_cast<Point&>((*this)[3]); }
    const Point& p() const                      { return static_cast<const Point&>((*this)[3]); }
    void zero()                                 { fill(*this, 0.f); }
    bool is_ident() const;
    bool invert();
    void make_right_handed()                    { if (dot(cross(v(0), v(1)), v(2))<0) v(0) = -v(0); }
    static Frame translation(const Vec3<float>& ar);
    static Frame rotation(int axis, float angle);
    static Frame scaling(const Vec3<float>& ar);
    static Frame identity();
};
Frame operator*(const Frame& f1, const Frame& f2);
inline Frame& operator*=(Frame& f1, const Frame& f2) { return f1 = f1*f2; }
bool invert(const Frame& fi, Frame& fo);
inline Frame inverse(const Frame& f)            { Frame fr; assertx(invert(f, fr)); return fr; }
inline Frame operator~(const Frame& f)          { return inverse(f); }
Frame transpose(const Frame& f);
std::ostream& operator<<(std::ostream& os, const Frame& f);
template<> HH_DECLARE_OSTREAM_EOL(Frame);

// *** Barycentric coordinates; sum to 1.f when expressing a Point as combination of 3 Points
//   or a Vector as a combination of 3 Vectors, but sums to 0.f when expressing a Vector as combination of 3 Points.
struct Bary : Vec3<float> {
    Bary()                                      = default;
    constexpr Bary(float x, float y, float z)   : Vec3<float>(x, y, z) { }
    constexpr Bary(Vec3<float> v)               : Vec3<float>(v) { }
    bool is_convex() const;
};

// *** Texture coordinates; usually defined over unit square [0, 1]^2.
//  Origin (0, 0) should be upper-left of an image although much code still assumes origin is at lower-left.
struct UV : Vec2<float> {
    UV()                                        = default;
    constexpr UV(float u, float v)              : Vec2<float>(u, v) { }
    constexpr UV(Vec2<float> v)                 : Vec2<float>(v) { }
};

// *** Misc operations

// Project v into plane orthogonal to unitdir.
Vector project_orthogonally(const Vector& v, const Vector& unitdir);

// More robust than acos(dot()) for small angles!
float angle_between_unit_vectors(const Vector& va, const Vector& vb);

// General affine combination of 4 points.  Note that interpolation domain is 3-dimensional (non-planar).
// "bary[3]"==1.f-bary[0]-bary[1]-bary[2]
template<typename T, int n> Vec<T,n> qinterp(const Vec<T,n>& a1, const Vec<T,n>& a2,
                                             const Vec<T,n>& a3, const Vec<T,n>& a4, const Bary& bary);

// Bilinear interpolation within 4 points.
// p3 - p2   v
//  |    |   ^
// p0 - p1   |  ->u
template<typename T, int n> Vec<T,n> bilerp(const Vec<T,n>& a0, const Vec<T,n>& a1,
                                            const Vec<T,n>& a2, const Vec<T,n>& a3, float u, float v);

// Spherical linear interpolation.  slerp(pa, pb, 1.f)==pa.
Point slerp(const Point& pa, const Point& pb, float ba);

// Spherical triangle area.
float spherical_triangle_area(const Vec3<Point>& pa);

// Get barycentric coordinates of point p within triangle pa, or return false if p not in plane of triangle.
bool get_bary(const Point& p, const Vec3<Point>& pa, Bary& bary);

// Given vector in plane of triangle, return its barycentric coordinates.
Bary vector_bary(const Vec3<Point>& pa, const Vector& vec);

// Given a triangle and barycentric coordinates, return the vector.
Vector bary_vector(const Vec3<Point>& pa, const Bary& bary);

// Convert degrees to radians.
template<typename T> constexpr T to_rad(T deg) {
    static_assert(std::is_floating_point<T>::value, ""); return deg*static_cast<T>(D_TAU/360);
}

// Convert radians to degrees.
template<typename T> constexpr T to_deg(T rad) {
    static_assert(std::is_floating_point<T>::value, ""); return rad*static_cast<T>(360/D_TAU);
}


//----------------------------------------------------------------------------

// *** Vector

inline bool Vector::normalize() {
    auto& v = *this;
    float sum2 = square(v[0])+square(v[1])+square(v[2]); if (!sum2) return false;
    v *= (1.f/sqrt(sum2)); return true;
}

inline constexpr Vector cross(const Vector& v1, const Vector& v2) {
    return Vector(v1[1]*v2[2]-v1[2]*v2[1], v1[2]*v2[0]-v1[0]*v2[2], v1[0]*v2[1]-v1[1]*v2[0]);
}

// *** Point

// *** Bary

inline bool Bary::is_convex() const {
    auto& self = *this;
    return (self[0]>=0.f && self[0]<=1.f &&
            self[1]>=0.f && self[1]<=1.f &&
            self[2]>=0.f && self[2]<=1.f);
}

// *** UV

// *** Misc

inline Vector project_orthogonally(const Vector& v, const Vector& unitdir) {
    ASSERTXX(is_unit(unitdir));
    return v-unitdir*dot(v, unitdir);
}

inline float angle_between_unit_vectors(const Vector& va, const Vector& vb) {
    ASSERTXX(is_unit(va) && is_unit(vb));
    float vdot = dot(va, vb);
    if (vdot>+.95f) {
        return asin(mag(cross(va, vb)));
    } else if (vdot<-.95f) {
        return TAU/2-asin(mag(cross(va, vb)));
    } else {
        return acos(vdot);
    }
}

template<typename T, int n> Vec<T,n> qinterp(const Vec<T,n>& a1, const Vec<T,n>& a2,
                                             const Vec<T,n>& a3, const Vec<T,n>& a4, const Bary& bary) {
    return bary[0]*a1 + bary[1]*a2 + bary[2]*a3 + (1.f-bary[0]-bary[1]-bary[2])*a4;
}

template<typename T, int n> Vec<T,n> bilerp(const Vec<T,n>& a0, const Vec<T,n>& a1,
                                            const Vec<T,n>& a2, const Vec<T,n>& a3, float u, float v) {
    // return qinterp(a0, a1, a2, a3, Bary((1.f-u)*(1.f-v), u*(1.f-v), u*v));
    return interp(interp(a0, a1, 1.f-u),
                  interp(a3, a2, 1.f-u), 1.f-v);
}

} // namespace hh

#endif // MESH_PROCESSING_LIBHH_GEOMETRY_H_
