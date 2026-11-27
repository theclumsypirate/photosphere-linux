#include "gui/shader_viewer.hpp"

#include <opencv2/imgproc.hpp>

#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ps::gui {
namespace {

constexpr const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPosition;
out vec2 vUv;
void main() {
    vUv = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform float uYaw;
uniform float uPitch;
uniform float uFov;
uniform float uAspect;
uniform float uLensFov;
uniform float uBlend;

vec2 fisheye(vec3 dir, vec3 axis, vec3 right, vec3 up, out float theta) {
    theta = acos(clamp(dot(dir, axis), -1.0, 1.0));
    float phi = atan(dot(dir, right), dot(dir, up));
    float r = theta / uLensFov;
    return vec2(0.5) + 0.5 * r * vec2(sin(phi), cos(phi));
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    float t = tan(uFov * 0.5);
    vec3 ray = normalize(vec3(ndc.x * uAspect * t, ndc.y * t, 1.0));
    float cy = cos(uYaw), sy = sin(uYaw);
    float cp = cos(uPitch), sp = sin(uPitch);
    mat3 yawMatrix = mat3(cy, 0.0, -sy, 0.0, 1.0, 0.0, sy, 0.0, cy);
    mat3 pitchMatrix = mat3(1.0, 0.0, 0.0, 0.0, cp, -sp, 0.0, sp, cp);
    vec3 dir = yawMatrix * pitchMatrix * ray;

    float thetaA, thetaB;
    vec2 uvA = fisheye(dir, vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                       vec3(0.0, 1.0, 0.0), thetaA);
    vec2 uvB = fisheye(dir, vec3(0.0, 0.0, -1.0), vec3(-1.0, 0.0, 0.0),
                       vec3(0.0, 1.0, 0.0), thetaB);

    float edgeA = 1.0 - thetaA / uLensFov;
    float edgeB = 1.0 - thetaB / uLensFov;
    float weightA = smoothstep(0.0, uBlend, edgeA);
    float weightB = smoothstep(0.0, uBlend, edgeB);
    if (weightA + weightB < 1e-4) {
        weightA = edgeA > edgeB ? 1.0 : 0.0;
        weightB = 1.0 - weightA;
    }
    vec3 colorA = texture(uTexture, vec2(uvA.x * 0.5, 1.0 - uvA.y)).rgb;
    vec3 colorB = texture(uTexture, vec2(0.5 + uvB.x * 0.5, 1.0 - uvB.y)).rgb;
    fragColor = vec4((colorA * weightA + colorB * weightB) / (weightA + weightB), 1.0);
}
)";

} // namespace

ShaderViewer::ShaderViewer(QWidget* parent) : QOpenGLWidget(parent) {}

ShaderViewer::~ShaderViewer() { makeCurrent(); vbo_ = 0; doneCurrent(); }

void ShaderViewer::initializeGL() {
    initializeOpenGLFunctions();
    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.link();
    const float quad[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f,
                          -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f};
    vao_.create();
    vao_.bind();
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    texture_.setWrapMode(QOpenGLTexture::ClampToEdge);
    texture_.setMinificationFilter(QOpenGLTexture::Linear);
    texture_.setMagnificationFilter(QOpenGLTexture::Linear);
}

void ShaderViewer::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

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
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!hasFrame_) return;
    makeCurrent();
    program_.bind();
    texture_.bind(0);
    vao_.bind();
    updateUniforms();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    vao_.release();
    program_.release();
}

void ShaderViewer::setFrame(const cv::Mat& sideBySide) {
    if (sideBySide.empty()) return;
    cv::Mat rgb;
    cv::cvtColor(sideBySide, rgb, cv::COLOR_BGR2RGB);
    makeCurrent();
    if (texture_.width() != rgb.cols || texture_.height() != rgb.rows) {
        texture_.destroy();
        texture_.setSize(rgb.cols, rgb.rows);
        texture_.create();
    }
    texture_.setData(QOpenGLTexture::RGB, QOpenGLTexture::UInt8, rgb.data);
    hasFrame_ = true;
    update();
    doneCurrent();
}

void ShaderViewer::resetView() {
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    fov_ = 90.0f;
    update();
}

void ShaderViewer::mousePressEvent(QMouseEvent* event) {
    lastPos_ = event->pos();
}

void ShaderViewer::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    const QPoint delta = event->pos() - lastPos_;
    yaw_ -= delta.x() * 0.35f;
    pitch_ = std::clamp(pitch_ + delta.y() * 0.35f, -89.0f, 89.0f);
    lastPos_ = event->pos();
    update();
}

void ShaderViewer::wheelEvent(QWheelEvent* event) {
    fov_ = std::clamp(fov_ - static_cast<float>(event->angleDelta().y()) / 120.0f * 5.0f,
                      20.0f, 150.0f);
    update();
}

} // namespace ps::gui
