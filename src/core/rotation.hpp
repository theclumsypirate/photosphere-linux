#pragma once

#include <array>
#include <cstddef>
#include <random>
#include <vector>

namespace ps {

using Mat3 = std::array<double, 9>;   // row major
using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>;   // w, x, y, z

namespace rotation {

Mat3 identity();
Mat3 multiply(const Mat3& a, const Mat3& b);
Mat3 transpose(const Mat3& m);
Vec3 apply(const Mat3& m, const Vec3& v);

double angle(const Mat3& a, const Mat3& b);
Mat3 rotationBetween(const Vec3& from, const Vec3& to);
Mat3 rotationAboutAxis(const Vec3& axis, double angleRad);

// The exact rotation taking two independent bearing pairs onto two others.
Mat3 basisRotation(const Vec3& a1, const Vec3& b1, const Vec3& a2, const Vec3& b2);

// Orthogonal Procrustes fit over many unit vector pairs.
Mat3 procrustes(const std::vector<Vec3>& a, const std::vector<Vec3>& b);

Mat3 quaternionToMatrix(const Quat& q);
Quat matrixToQuaternion(const Mat3& m);
Mat3 weightedMeanRotation(const std::vector<Mat3>& rotations,
                          const std::vector<double>& weights);

struct RotationEdge {
    int from = 0;
    int to = 0;
    Mat3 rotation = identity();
    double weight = 1.0;
};

// Gauss-Seidel rotation averaging. Frame 0 stays at its initial value.
std::vector<Mat3> averageRotations(const std::vector<Mat3>& initial,
                                   const std::vector<RotationEdge>& edges,
                                   int iterations);

struct RotationEstimate {
    Mat3 rotation = identity();
    std::vector<int> inliers;
    double residualRadians = 0.0;
};

// Two-point RANSAC over matched unit bearings: to[i] ~= R * from[i].
RotationEstimate estimateRotation(const std::vector<Vec3>& from,
                                  const std::vector<Vec3>& to,
                                  double thresholdRadians,
                                  int maxIterations,
                                  std::mt19937& rng);

// Vector helpers shared with the geometry code.
double dot3(const Vec3& a, const Vec3& b);
Vec3 cross3(const Vec3& a, const Vec3& b);
double norm3(const Vec3& a);
Vec3 normalize3(const Vec3& a);
Vec3 scale3(const Vec3& a, double s);
Vec3 add3(const Vec3& a, const Vec3& b);
Vec3 sub3(const Vec3& a, const Vec3& b);

} // namespace rotation
} // namespace ps
