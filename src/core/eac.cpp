#include "core/eac.hpp"

#include "core/spherical.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ps {
namespace {

constexpr double kPi = 3.14159265358979323846;

enum Direction { RIGHT, LEFT, UP, DOWN, FRONT, BACK };
enum Rotation { ROT_0, ROT_90, ROT_180, ROT_270 };

int faceOrder(Direction direction) {
    switch (direction) {
        case RIGHT: return 2; // TOP_RIGHT
        case LEFT:  return 0; // TOP_LEFT
        case UP:    return 5; // BOTTOM_RIGHT
        case DOWN:  return 3; // BOTTOM_LEFT
        case FRONT: return 1; // TOP_MIDDLE
        case BACK:  return 4; // BOTTOM_MIDDLE
    }
    return 0;
}

Rotation faceRotation(int face) {
    if (face == 3 || face == 5) return ROT_270;
    if (face == 4) return ROT_90;
    return ROT_0;
}

void rotateCubeFace(double& uf, double& vf, Rotation rotation) {
    double tmp;
    switch (rotation) {
        case ROT_0: break;
        case ROT_90: tmp = uf; uf = -vf; vf = tmp; break;
        case ROT_180: uf = -uf; vf = -vf; break;
        case ROT_270: tmp = -uf; uf = vf; vf = tmp; break;
    }
}

// ffmpeg v360 xyz_to_cube, with the direction order used by EAC input.
void xyzToCube(const Vec3& world, double& uf, double& vf, int& face) {
    // Equirect directions are x east, y north, z up. v360 uses x right,
    // y up, z front.
    const double x = world[0], y = world[2], z = world[1];
    const double phi = std::atan2(x, z);
    const double theta = std::asin(std::clamp(y, -1.0, 1.0));
    Direction direction;
    double phiNorm;
    constexpr double kQuarter = kPi / 4.0;
    if (phi >= -kQuarter && phi < kQuarter) {
        direction = FRONT;
        phiNorm = phi;
    } else if (phi >= -(kPi / 2.0 + kQuarter) && phi < -kQuarter) {
        direction = LEFT;
        phiNorm = phi + kPi / 2.0;
    } else if (phi >= kQuarter && phi < kPi / 2.0 + kQuarter) {
        direction = RIGHT;
        phiNorm = phi - kPi / 2.0;
    } else {
        direction = BACK;
        phiNorm = phi + (phi > 0.0 ? -kPi : kPi);
    }
    const double threshold = std::atan(std::cos(phiNorm));
    if (theta > threshold) direction = DOWN;
    else if (theta < -threshold) direction = UP;

    switch (direction) {
        case RIGHT: uf = -z / x; vf = y / x; break;
        case LEFT:  uf = -z / x; vf = -y / x; break;
        case UP:    uf = -x / y; vf = -z / y; break;
        case DOWN:  uf = x / y;  vf = -z / y; break;
        case FRONT: uf = x / z;  vf = y / z; break;
        case BACK:  uf = x / z;  vf = -y / z; break;
    }
    face = faceOrder(direction);
    rotateCubeFace(uf, vf, faceRotation(face));
}

} // namespace

bool isEacFrameSize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const double ratio = static_cast<double>(width) / height;
    return ratio >= 2.5 && ratio <= 3.5;
}

cv::Mat eacToEquirect(const cv::Mat& eac, int outWidth) {
    if (eac.empty()) return {};
    if (outWidth <= 0) outWidth = eac.cols;
    const int outHeight = outWidth / 2;
    if (outWidth <= 0 || outHeight <= 0) return {};

    const double width = static_cast<double>(eac.cols);
    const double height = static_cast<double>(eac.rows);
    const double uPad = 2.0 / width;
    const double vPad = 2.0 / height;

    cv::Mat mapX(outHeight, outWidth, CV_32FC1);
    cv::Mat mapY(outHeight, outWidth, CV_32FC1);
    for (int row = 0; row < outHeight; ++row) {
        const double latitude = equirect::latitudeDegrees(row, outHeight);
        auto* mx = mapX.ptr<float>(row);
        auto* my = mapY.ptr<float>(row);
        for (int col = 0; col < outWidth; ++col) {
            const double longitude = equirect::longitudeDegrees(col, outWidth);
            const Vec3 direction = equirect::direction(longitude, latitude);
            double uf = 0.0, vf = 0.0;
            int face = 0;
            xyzToCube(direction, uf, vf, face);
            const double fu = (2.0 / kPi) * std::atan(uf) + 0.5;
            const double fv = (2.0 / kPi) * std::atan(vf) + 0.5;
            const double sourceU = (fu + (face % 3)) * (1.0 - 2.0 * uPad) / 3.0 + uPad;
            const double sourceV = fv * (0.5 - 2.0 * vPad) + vPad +
                                   0.5 * (face / 3);
            mx[col] = static_cast<float>(sourceU * width - 0.5);
            my[col] = static_cast<float>(sourceV * height - 0.5);
        }
    }
    cv::Mat out;
    cv::remap(eac, out, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    // GoPro stores the two EAC halves with their poles inverted relative to
    // the v360 face order, so the final panorama needs a vertical flip.
    cv::flip(out, out, 0);
    return out;
}

} // namespace ps
