// Math types. `Matrix4` mirrors Alternativa's alternativa.types.Matrix4 (affine 3x4, row-major,
// elements a..l) and is used everywhere the original code does matrix work, so that exported
// numbers match. `Mat4f` is a column-major float matrix for OpenGL.
#pragma once
#include <cmath>
#include <limits>

namespace ae {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG1 = PI / 180.0;
constexpr double DEG5 = PI / 36.0;
constexpr double DEG30 = PI / 6.0;
constexpr double DEG90 = PI / 2.0;
inline double toRadian(double deg) { return deg * PI / 180.0; }
inline double toDegree(double rad) { return rad * 180.0 / PI; }

struct Vec2 {
    double x = 0, y = 0;
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double lengthSqr() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt(lengthSqr()); }
    Vec3 normalized() const {
        double l = length();
        return l > 0 ? Vec3{x / l, y / l, z / l} : Vec3{};
    }
    bool isNaN() const { return std::isnan(x) || std::isnan(y) || std::isnan(z); }
};

struct Matrix4 {
    double a = 1, b = 0, c = 0, d = 0;
    double e = 0, f = 1, g = 0, h = 0;
    double i = 0, j = 0, k = 1, l = 0;

    static Matrix4 identity() { return {}; }

    // M = T * Rz * Ry * Rx * S  (Alternativa Object3D composition)
    static Matrix4 compose(double x, double y, double z, double rx, double ry, double rz,
                           double sx = 1, double sy = 1, double sz = 1) {
        double cx = std::cos(rx), sxr = std::sin(rx);
        double cy = std::cos(ry), syr = std::sin(ry);
        double cz = std::cos(rz), szr = std::sin(rz);
        double czsy = cz * syr, szsy = szr * syr;
        Matrix4 m;
        m.a = cz * cy * sx;
        m.b = (czsy * sxr - szr * cx) * sy;
        m.c = (czsy * cx + szr * sxr) * sz;
        m.d = x;
        m.e = szr * cy * sx;
        m.f = (szsy * sxr + cz * cx) * sy;
        m.g = (szsy * cx - cz * sxr) * sz;
        m.h = y;
        m.i = -syr * sx;
        m.j = cy * sxr * sy;
        m.k = cy * cx * sz;
        m.l = z;
        return m;
    }

    // Returns A * B  (apply B first, then A).
    static Matrix4 mul(const Matrix4& A, const Matrix4& B) {
        Matrix4 r;
        r.a = A.a * B.a + A.b * B.e + A.c * B.i;
        r.b = A.a * B.b + A.b * B.f + A.c * B.j;
        r.c = A.a * B.c + A.b * B.g + A.c * B.k;
        r.d = A.a * B.d + A.b * B.h + A.c * B.l + A.d;
        r.e = A.e * B.a + A.f * B.e + A.g * B.i;
        r.f = A.e * B.b + A.f * B.f + A.g * B.j;
        r.g = A.e * B.c + A.f * B.g + A.g * B.k;
        r.h = A.e * B.d + A.f * B.h + A.g * B.l + A.h;
        r.i = A.i * B.a + A.j * B.e + A.k * B.i;
        r.j = A.i * B.b + A.j * B.f + A.k * B.j;
        r.k = A.i * B.c + A.j * B.g + A.k * B.k;
        r.l = A.i * B.d + A.j * B.h + A.k * B.l + A.l;
        return r;
    }

    // this = m * this  (Alternativa Matrix4.combine)
    void combine(const Matrix4& m) { *this = mul(m, *this); }

    // this = R(rx,ry,rz) * this  (Alternativa Matrix4.rotate)
    void rotate(double rx, double ry, double rz) { *this = mul(compose(0, 0, 0, rx, ry, rz), *this); }

    void translate(double x, double y, double z) { d += x; h += y; l += z; }

    void setVectors(const Vec3& X, const Vec3& Y, const Vec3& Z, const Vec3& P) {
        a = X.x; e = X.y; i = X.z;
        b = Y.x; f = Y.y; j = Y.z;
        c = Z.x; g = Z.y; k = Z.z;
        d = P.x; h = P.y; l = P.z;
    }

    Matrix4 inverted() const {
        double det = -c * f * i + b * g * i + c * e * j - a * g * j - b * e * k + a * f * k;
        Matrix4 r;
        r.a = (-g * j + f * k) / det;
        r.b = (c * j - b * k) / det;
        r.c = (-c * f + b * g) / det;
        r.d = (d * g * j - c * h * j - d * f * k + b * h * k + c * f * l - b * g * l) / det;
        r.e = (g * i - e * k) / det;
        r.f = (-c * i + a * k) / det;
        r.g = (c * e - a * g) / det;
        r.h = (c * h * i - d * g * i + d * e * k - a * h * k - c * e * l + a * g * l) / det;
        r.i = (-f * i + e * j) / det;
        r.j = (b * i - a * j) / det;
        r.k = (-b * e + a * f) / det;
        r.l = (d * f * i - b * h * i - d * e * j + a * h * j + b * e * l - a * f * l) / det;
        return r;
    }

    Vec3 transform(const Vec3& p) const {
        return {a * p.x + b * p.y + c * p.z + d, e * p.x + f * p.y + g * p.z + h,
                i * p.x + j * p.y + k * p.z + l};
    }
    Vec3 transformDir(const Vec3& p) const {
        return {a * p.x + b * p.y + c * p.z, e * p.x + f * p.y + g * p.z, i * p.x + j * p.y + k * p.z};
    }
    Vec3 position() const { return {d, h, l}; }
    Vec3 axisX() const { return {a, e, i}; }
    Vec3 axisY() const { return {b, f, j}; }
    Vec3 axisZ() const { return {c, g, k}; }

    // Alternativa Matrix4.getRotations
    Vec3 getRotations() const {
        Vec3 r;
        if (-1 < i && i < 1) {
            r.x = std::atan2(j, k);
            r.y = -std::asin(i);
            r.z = std::atan2(e, a);
        } else {
            r.x = 0;
            r.y = 0.5 * (i <= -1 ? PI : -PI);
            r.z = std::atan2(-b, f);
        }
        return r;
    }
};

// Column-major 4x4 float matrix (OpenGL convention).
struct Mat4f {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4f fromAffine(const Matrix4& M) {
        Mat4f r;
        r.m[0] = (float)M.a; r.m[4] = (float)M.b; r.m[8] = (float)M.c;  r.m[12] = (float)M.d;
        r.m[1] = (float)M.e; r.m[5] = (float)M.f; r.m[9] = (float)M.g;  r.m[13] = (float)M.h;
        r.m[2] = (float)M.i; r.m[6] = (float)M.j; r.m[10] = (float)M.k; r.m[14] = (float)M.l;
        r.m[3] = 0;          r.m[7] = 0;          r.m[11] = 0;          r.m[15] = 1;
        return r;
    }
    static Mat4f mul(const Mat4f& A, const Mat4f& B) {
        Mat4f r;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += A.m[k * 4 + row] * B.m[col * 4 + k];
                r.m[col * 4 + row] = s;
            }
        return r;
    }
    static Mat4f ortho(float l, float r, float b, float t, float n, float f) {
        Mat4f o;
        o.m[0] = 2 / (r - l);
        o.m[5] = 2 / (t - b);
        o.m[10] = -2 / (f - n);
        o.m[12] = -(r + l) / (r - l);
        o.m[13] = -(t + b) / (t - b);
        o.m[14] = -(f + n) / (f - n);
        return o;
    }
};

}  // namespace ae
