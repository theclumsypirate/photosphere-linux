#include "gui/viewer_widget.hpp"

#include <QIcon>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ps::gui {
namespace {

constexpr const char* kVertexShader = R"(
#version 330 core
in vec3 aPosition;
uniform mat4 uProjection;
uniform mat4 uView;
out vec3 vDirection;
void main() {
    vDirection = aPosition;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(
#version 330 core
uniform sampler2D uTexture;
in vec3 vDirection;
out vec4 fragColor;
const float PI = 3.141592653589793;
void main() {
    vec3 d = normalize(vDirection);
    float lon = atan(d.x, d.y);
    float lat = asin(clamp(d.z, -1.0, 1.0));
    vec2 uv = vec2(lon / (2.0 * PI) + 0.5, 0.5 - lat / PI);
    fragColor = texture(uTexture, uv);
}
)";

} // namespace

ViewerWidget::ViewerWidget(QWidget* parent) : QOpenGLWidget(parent) {
    snapshotButton_ = new QPushButton("Save view", this);
    snapshotButton_->setIcon(QIcon(":/icons/snapshot.png"));
    snapshotButton_->setIconSize(QSize(24, 24));
    snapshotButton_->setToolTip("Save the current 360 view as an image");
    snapshotButton_->setFixedSize(120, 40);
    snapshotButton_->setStyleSheet(
        "QPushButton { background: rgba(30,32,42,220); color: white; "
        "border: 1px solid #555; border-radius: 6px; } "
        "QPushButton:hover { background: rgba(70,74,92,240); }");
    snapshotButton_->move(width() - 132, height() - 52);
    snapshotButton_->raise();
    snapshotButton_->show();
    connect(snapshotButton_, &QPushButton::clicked,
            this, &ViewerWidget::snapshotRequested);

    savePanoButton_ = new QPushButton("Save panorama", this);
    savePanoButton_->setIcon(QIcon(":/icons/icon.png"));
    savePanoButton_->setIconSize(QSize(20, 20));
    savePanoButton_->setToolTip("Save the full equirectangular panorama");
    savePanoButton_->setFixedSize(150, 40);
    savePanoButton_->setStyleSheet(
        "QPushButton { background: rgba(30,32,42,220); color: white; "
        "border: 1px solid #555; border-radius: 6px; } "
        "QPushButton:hover { background: rgba(70,74,92,240); }");
    savePanoButton_->move(width() - 292, height() - 52);
    savePanoButton_->raise();
    savePanoButton_->show();
    connect(savePanoButton_, &QPushButton::clicked,
            this, &ViewerWidget::panoramaSaveRequested);

    fovSlider_ = new QSlider(Qt::Horizontal, this);
    fovSlider_->setRange(15, 120);
    fovSlider_->setValue(75);
    fovSlider_->setFixedSize(180, 40);
    fovSlider_->setToolTip("Field of view");
    fovSlider_->setStyleSheet(
        "QSlider { background: rgba(30,32,42,220); border-radius: 6px; }");
    fovSlider_->move(12, height() - 52);
    fovSlider_->raise();
    fovSlider_->show();
    connect(fovSlider_, &QSlider::valueChanged, this, [this](int value) {
        fov_ = static_cast<float>(value);
        resizeGL(width(), height());
        update();
    });
}

ViewerWidget::~ViewerWidget() {
    makeCurrent();
    vao_.destroy();
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    doneCurrent();
}

void ViewerWidget::buildSphere() {
    const int stacks = 48;
    const int slices = 96;
    vertices_.clear();
    indices_.clear();
    const double pi = 3.14159265358979323846;
    for (int lat = 0; lat <= stacks; ++lat) {
        const double phi = -pi / 2.0 + pi * lat / stacks;
        for (int lon = 0; lon <= slices; ++lon) {
            const double theta = 2.0 * pi * lon / slices;
            vertices_.push_back(static_cast<float>(std::sin(theta) * std::cos(phi)));
            vertices_.push_back(static_cast<float>(std::cos(theta) * std::cos(phi)));
            vertices_.push_back(static_cast<float>(std::sin(phi)));
        }
    }
    for (int lat = 0; lat < stacks; ++lat) {
        for (int lon = 0; lon < slices; ++lon) {
            const unsigned int a = lat * (slices + 1) + lon;
            const unsigned int b = a + slices + 1;
            indices_.push_back(a); indices_.push_back(b); indices_.push_back(a + 1);
            indices_.push_back(b); indices_.push_back(b + 1); indices_.push_back(a + 1);
        }
    }
}

void ViewerWidget::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    buildSphere();

    program_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    program_.link();

    vao_.create();
    vao_.bind();
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices_.size() * sizeof(float)),
                 vertices_.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices_.size() * sizeof(unsigned int)),
                 indices_.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    vao_.release();
}

void ViewerWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    projection_.setToIdentity();
    projection_.perspective(fov_, static_cast<float>(w) / std::max(1, h), 0.01f, 10.0f);
}

void ViewerWidget::paintGL() {
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!hasTexture_) return;
    program_.bind();
    program_.setUniformValue("uProjection", projection_);
    program_.setUniformValue("uView", view_);
    texture_.bind(0);
    program_.setUniformValue("uTexture", 0);
    vao_.bind();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()),
                   GL_UNSIGNED_INT, nullptr);
    vao_.release();
    program_.release();
}

void ViewerWidget::setPanorama(const cv::Mat& bgr) {
    if (bgr.empty()) return;
    makeCurrent();
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    if (!hasTexture_ || texture_.width() != rgb.cols ||
        texture_.height() != rgb.rows) {
        texture_.destroy();
        texture_.setSize(rgb.cols, rgb.rows);
        texture_.setFormat(QOpenGLTexture::RGB8_UNorm);
        texture_.allocateStorage();
        hasTexture_ = true;
    }
    texture_.setData(QOpenGLTexture::RGB, QOpenGLTexture::UInt8, rgb.data);
    texture_.setMinificationFilter(QOpenGLTexture::Linear);
    texture_.setMagnificationFilter(QOpenGLTexture::Linear);
    texture_.setWrapMode(QOpenGLTexture::Repeat);
    doneCurrent();
    update();
}

void ViewerWidget::resetView() {
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    fov_ = 75.0f;
    if (fovSlider_) fovSlider_->setValue(75);
    resizeGL(width(), height());
    updateViewMatrix();
    update();
}

void ViewerWidget::updateViewMatrix() {
    const double yr = yaw_ * 3.14159265358979323846 / 180.0;
    const double pr = pitch_ * 3.14159265358979323846 / 180.0;
    const QVector3D direction(static_cast<float>(std::sin(yr) * std::cos(pr)),
                              static_cast<float>(std::cos(yr) * std::cos(pr)),
                              static_cast<float>(std::sin(pr)));
    view_.setToIdentity();
    view_.lookAt(QVector3D(0, 0, 0), direction, QVector3D(0, 0, 1));
}

void ViewerWidget::mousePressEvent(QMouseEvent* event) {
    dragging_ = true;
    lastPos_ = event->pos();
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint delta = event->pos() - lastPos_;
    lastPos_ = event->pos();
    yaw_ -= delta.x() * 0.3f;
    pitch_ = std::clamp(pitch_ + delta.y() * 0.3f, -89.0f, 89.0f);
    updateViewMatrix();
    update();
}

void ViewerWidget::resizeEvent(QResizeEvent* event) {
    QOpenGLWidget::resizeEvent(event);
    if (snapshotButton_)
        snapshotButton_->move(width() - 132, height() - 52);
    if (savePanoButton_)
        savePanoButton_->move(width() - 292, height() - 52);
    if (fovSlider_)
        fovSlider_->move(12, height() - 52);
}

QPixmap ViewerWidget::currentView() {
    makeCurrent();
    const QImage image = grabFramebuffer();
    doneCurrent();
    return QPixmap::fromImage(image);
}

void ViewerWidget::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    fov_ = std::clamp(fov_ + steps * 5.0f, 15.0f, 120.0f);
    if (fovSlider_) fovSlider_->setValue(static_cast<int>(fov_));
    resizeGL(width(), height());
    update();
}

} // namespace ps::gui
