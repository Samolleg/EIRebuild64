#pragma once
#include <cmath>
#include <algorithm>

// Homogeneous screen coordinates. Clip triangles at the eye plane before division;
// do not include unused packed lanes or vertices behind the camera in hit bounds.
namespace FigureScreenBounds {
struct Point { double x, y, w; };
struct Bounds {
    bool valid = false;
    double left = 0, top = 0, right = 0, bottom = 0;
    void AddTriangle(const Point *triangle) {
        Point clipped[4]; int count = 0;
        const double nearW = 0.0001;
        for (int i = 0; i < 3; ++i) {
            const Point a = triangle[i], b = triangle[(i + 1) % 3];
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.w)) return;
            if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.w)) return;
            const bool insideA = a.w >= nearW, insideB = b.w >= nearW;
            if (insideA) clipped[count++] = a;
            if (insideA != insideB) {
                const double t = (nearW - a.w) / (b.w - a.w);
                clipped[count++] = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), nearW};
            }
        }
        for (int i = 0; i < count; ++i) {
            const double x = (std::max)(-1000000.0, (std::min)(1000000.0, clipped[i].x / clipped[i].w));
            const double y = (std::max)(-1000000.0, (std::min)(1000000.0, clipped[i].y / clipped[i].w));
            if (!valid) {left = right = x; top = bottom = y; valid = true;}
            else {left = (std::min)(left,x); right = (std::max)(right,x);
                  top = (std::min)(top,y); bottom = (std::max)(bottom,y);}
        }
    }
    void AddBox(const Point (&vertices)[8]) {
        static constexpr unsigned faces[][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},{0,4,5},{0,5,1},
            {2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
        for (const auto& face:faces) {
            const Point triangle[]={vertices[face[0]],vertices[face[1]],vertices[face[2]]};
            AddTriangle(triangle);
        }
    }
    int Left() const { return int(std::floor(left)); }
    int Top() const { return int(std::floor(top)); }
    int Right() const { return int(std::floor(right)) + 1; }
    int Bottom() const { return int(std::floor(bottom)) + 1; }
};
}
