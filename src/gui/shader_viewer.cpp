#include "gui/shader_viewer.hpp"

#include <opencv2/imgproc.hpp>

#include <QMouseEvent>
#include <QWheelEvent>
#include <QtDebug>

#include <algorithm>
#include <cmath>

namespace ps::gui {
namespace {

// Desktop GL 2.1 and OpenGL ES 2/3 both accept this. A 330 core shader fails
// on Wayland ES contexts and leaves the widget black.
constexpr const char* kVertexShader = R"(
attribute vec2 corner;
varying vec2 ndc;
void main() { ndc = corner; gl_Position = vec4(corner, 0.0, 1.0); }
)";

constexpr const char* kFragmentShader = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 ndc;
uniform sampler2D uTexture;
uniform float uYaw, uPitch, uFov, uAspect, uLensFov, uBlend;

vec2 fisheye(vec3 dir, vec3 axis, vec3 right, vec3 up, out float theta) {
    theta = acos(clamp(dot(dir, axis), -1.0, 1.0));
    float phi = atan(dot(dir, right), dot(dir, up));
    float r = theta / uLensFov;
    return vec2(0.5) + 0.5 * r * vec2(sin(phi), cos(phi));
}

void main() {
    vec2 p = ndc;
    float t = tan(uFov * 0.5);
    vec3 ray = normalize(vec3(p.x * uAspect * t, p.y * t, 1.0));
    float cy = cos(uYaw), sy = sin(uYaw);
    float cp = cos(uPitch), sp = sin(uPitch);
    vec3 d = vec3(ray.x, ray.y * cp + ray.z * sp, -ray.y * sp + ray.z * cp);
    d = vec3(d.x * cy + d.z * sy, d.y, -d.x * sy + d.z * cy);
    float thetaA, thetaB;
    vec2 uvA = fisheye(d, vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                       vec3(0.0, 1.0, 0.0), thetaA);
    vec2 uvB = fisheye(d, vec3(0.0, 0.0, -1.0), vec3(-1.0, 0.0, 0.0),
                       vec3(0.0, 1.0, 0.0), thetaB);
    float wa = smoothstep(0.0, uBlend, 1.0 - thetaA / uLensFov);
    float wb = smoothstep(0.0, uBlend, 1.0 - thetaB / uLensFov);
    if (wa + wb < 0.0001) { wa = 1.0; wb = 0.0; }
    vec3 ca = texture2D(uTexture, vec2(uvA.x * 0.5, 1.0 - uvA.y)).rgb;
    vec3 cb = texture2D(uTexture, vec2(0.5 + uvB.x * 0.5, 1.0 - uvB.y)).rgb;
    if (wa > 0.0 && wb > 0.0) {
        // Where the lenses disagree, a plain mix shows a ghost. Favour the
        // more reliable lens instead, as in the real-time stitcher.
        float diff = length(ca - cb);
        float p = 1.0 + 12.0 * smoothstep(0.06, 0.2, diff);
        float m = max(wa, wb);
        wa = pow(max(wa / m, 0.0001), p) * m;
        wb = pow(max(wb / m, 0.0001), p) * m;
    }
    gl_FragColor = vec4((ca * wa + cb * wb) / (wa + wb), 1.0);
}
)";

} // namespace

ShaderViewer::ShaderViewer(QWidget* parent) : QOpenGLWidget(parent) {}

ShaderViewer::~ShaderViewer() {
    makeCurrent();
    for (QOpenGLTexture* texture : textures_) delete texture;
    textures_.clear();
    doneCurrent();
}

void ShaderViewer::initializeGL() {
    initializeOpenGLFunctions();
    if (!program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
        qWarning("GPU preview shader failed: %s", qPrintable(program_.log()));
        return;
    }
    program_.bindAttributeLocation("corner", 0);
    if (!program_.link()) {
        qWarning("GPU preview shader link failed: %s", qPrintable(program_.log()));
        return;
    }
    const float corners[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    vao_.create();
    vao_.bind();
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    program_.bind();
    program_.enableAttributeArray(0);
    program_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    program_.release();
    vao_.release();
    glReady_ = true;
}

void ShaderViewer::uploadFrame() {
    if (pendingImage_.isNull()) return;
    // Constructing the texture from QImage is the upload path that works with
    // every Qt/GL combination here, including Wayland ES contexts.
    for (QOpenGLTexture* texture : textures_) delete texture;
    textures_.clear();
    auto* texture = new QOpenGLTexture(pendingImage_,
                                       QOpenGLTexture::GenerateMipMaps);
    texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    textures_.push_back(texture);
    pendingImage_ = QImage();
    hasFrame_ = true;
}

void ShaderViewer::setFrame(const cv::Mat& sideBySide) {
    if (sideBySide.empty()) return;
    cv::Mat rgb;
    cv::cvtColor(sideBySide, rgb, cv::COLOR_BGR2RGB);
    pendingImage_ = QImage(rgb.data, rgb.cols, rgb.rows,
                           static_cast<qsizetype>(rgb.step),
                           QImage::Format_RGB888).copy();
    update();
}

void ShaderViewer::updateUniforms() {
    program_.setUniformValue("uTexture", 0);
    program_.setUniformValue("uYaw", yaw_ * static_cast<float>(M_PI) / 180.0f);
    program_.setUniformValue("uPitch", pitch_ * static_cast<float>(M_PI) / 180.0f);
    program_.setUniformValue("uFov", fov_ * static_cast<float>(M_PI) / 180.0f);
    program_.setUniformValue("uAspect", static_cast<float>(width()) /
                                            std::max(1, height()));
    program_.setUniformValue("uLensFov", lensFov_ * static_cast<float>(M_PI) / 360.0f);
    program_.setUniformValue("uBlend", 0.08f);
}

void ShaderViewer::paintGL() {
    glViewport(0, 0, width(), height());
    glClearColor(0.02f, 0.03f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!glReady_) return;
    if (!pendingImage_.isNull()) uploadFrame();
    if (!hasFrame_ || textures_.empty()) return;
    program_.bind();
    textures_.front()->bind(0);
    vao_.bind();
    updateUniforms();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vao_.release();
    program_.release();
}

void ShaderViewer::resetView() {
    yaw_ = 0.f;
    pitch_ = 0.f;
    fov_ = 90.f;
    update();
}

void ShaderViewer::mousePressEvent(QMouseEvent* event) { lastPos_ = event->pos(); }

void ShaderViewer::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    const QPoint delta = event->pos() - lastPos_;
    yaw_ -= delta.x() * 0.35f;
    pitch_ = std::clamp(pitch_ + delta.y() * 0.35f, -89.f, 89.f);
    lastPos_ = event->pos();
    update();
}

void ShaderViewer::wheelEvent(QWheelEvent* event) {
    fov_ = std::clamp(fov_ - static_cast<float>(event->angleDelta().y()) / 120.f * 5.f,
                      20.f, 150.f);
    update();
}

} // namespace ps::gui
