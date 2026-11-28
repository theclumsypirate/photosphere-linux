#include "gui/viewer_widget.hpp"

#include <QIcon>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QResizeEvent>
#include <QSignalBlocker>
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
        updateProjection();
        update();
    });

    stabilizeButton_ = new QPushButton("Stabilize", this);
    stabilizeButton_->setToolTip("Level the panorama using IMU telemetry");
    stabilizeButton_->setFixedSize(110, 40);
    stabilizeButton_->setStyleSheet(
        "QPushButton { background: rgba(30,32,42,220); color: white; "
        "border: 1px solid #555; border-radius: 6px; } "
        "QPushButton:hover { background: rgba(70,74,92,240); }");
    stabilizeButton_->move(200, height() - 52);
    stabilizeButton_->raise();
    stabilizeButton_->show();
    connect(stabilizeButton_, &QPushButton::clicked,
            this, &ViewerWidget::stabilizeRequested);
}

ViewerWidget::~ViewerWidget() {
    if (!context() || !context()->isValid()) return;
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
    glReady_ = true;
}

void ViewerWidget::updateProjection() {
    projection_.setToIdentity();
    projection_.perspective(fov_,
                            static_cast<float>(width()) / std::max(1, height()),
                            0.01f, 10.0f);
}

void ViewerWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    updateProjection();
    updateProjection2();
}

void ViewerWidget::paintGL() {
    glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!pendingPanorama_.empty()) {
        const cv::Mat image = pendingPanorama_;
        pendingPanorama_.release();
        setPanorama(image);
    }
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

    if (!pendingSecondary_.empty()) {
        const cv::Mat image = pendingSecondary_;
        pendingSecondary_.release();
        setSecondaryPanorama(image);
    }
    if (pipEnabled_ && hasTexture2_) {
        const QRect rect = secondaryRect();
        glViewport(rect.x(), height() - rect.y() - rect.height(),
                   rect.width(), rect.height());
        glClear(GL_DEPTH_BUFFER_BIT);
        program_.bind();
        program_.setUniformValue("uProjection", projection2_);
        program_.setUniformValue("uView", view2_);
        texture2_.bind(0);
        program_.setUniformValue("uTexture", 0);
        vao_.bind();
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices_.size()),
                       GL_UNSIGNED_INT, nullptr);
        vao_.release();
        program_.release();
    }
}

void ViewerWidget::setPanorama(const cv::Mat& bgr) {
    if (bgr.empty()) return;
    if (!glReady_) {
        pendingPanorama_ = bgr.clone();
        update();
        return;
    }
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

void ViewerWidget::setView(double yawDegrees, double pitchDegrees,
                          double fovDegrees) {
    yaw_ = static_cast<float>(yawDegrees);
    pitch_ = static_cast<float>(std::clamp(pitchDegrees, -89.0, 89.0));
    fov_ = static_cast<float>(std::clamp(fovDegrees, 15.0, 120.0));
    if (fovSlider_) {
        const QSignalBlocker blocker(fovSlider_);
        fovSlider_->setValue(static_cast<int>(fov_));
    }
    updateProjection();
    updateViewMatrix();
    update();
}

void ViewerWidget::setOverlayButtonsVisible(bool visible) {
    snapshotButton_->setVisible(visible);
    savePanoButton_->setVisible(visible);
    fovSlider_->setVisible(visible);
}

void ViewerWidget::resetView() {
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    fov_ = 75.0f;
    if (fovSlider_) {
        const QSignalBlocker blocker(fovSlider_);
        fovSlider_->setValue(75);
    }
    updateProjection();
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
    editingSecondary_ = pipEnabled_ && hasTexture2_ &&
                        secondaryRect().contains(event->pos());
    lastPos_ = event->pos();
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    const QPoint delta = event->pos() - lastPos_;
    lastPos_ = event->pos();
    float& yaw = editingSecondary_ ? yaw2_ : yaw_;
    float& pitch = editingSecondary_ ? pitch2_ : pitch_;
    yaw -= delta.x() * 0.3f;
    pitch = std::clamp(pitch + delta.y() * 0.3f, -89.0f, 89.0f);
    if (editingSecondary_) updateViewMatrix2();
    else updateViewMatrix();
    update();
}

void ViewerWidget::resizeEvent(QResizeEvent* event) {
    // During embedding/show, Qt can deliver a resize before the GL context is
    // created. Calling the base then dereferences null context functions.
    if (context() && context()->isValid())
        QOpenGLWidget::resizeEvent(event);
    else
        QWidget::resizeEvent(event);
    if (snapshotButton_)
        snapshotButton_->move(width() - 132, height() - 52);
    if (savePanoButton_)
        savePanoButton_->move(width() - 292, height() - 52);
    if (fovSlider_)
        fovSlider_->move(12, height() - 52);
    if (stabilizeButton_)
        stabilizeButton_->move(200, height() - 52);
}

QPixmap ViewerWidget::currentView() {
    makeCurrent();
    const QImage image = grabFramebuffer();
    doneCurrent();
    return QPixmap::fromImage(image);
}

void ViewerWidget::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    const bool secondary = pipEnabled_ && hasTexture2_ &&
                           secondaryRect().contains(event->position().toPoint());
    float& fov = secondary ? fov2_ : fov_;
    fov = std::clamp(fov + steps * 5.0f, 15.0f, 120.0f);
    if (!secondary && fovSlider_) {
        const QSignalBlocker blocker(fovSlider_);
        fovSlider_->setValue(static_cast<int>(fov));
    }
    if (secondary) updateProjection2();
    else updateProjection();
    update();
}


QRect ViewerWidget::secondaryRect() const {
    const int w = std::max(160, width() / 3);
    const int h = std::max(90, height() / 3);
    return QRect(width() - w - 8, height() - h - 8, w, h);
}

void ViewerWidget::updateProjection2() {
    const QRect r = secondaryRect();
    projection2_.setToIdentity();
    projection2_.perspective(fov2_,
                             static_cast<float>(r.width()) / std::max(1, r.height()),
                             0.01f, 10.0f);
}

void ViewerWidget::updateViewMatrix2() {
    const double yr = yaw2_ * 3.14159265358979323846 / 180.0;
    const double pr = pitch2_ * 3.14159265358979323846 / 180.0;
    const QVector3D direction(static_cast<float>(std::sin(yr) * std::cos(pr)),
                              static_cast<float>(std::cos(yr) * std::cos(pr)),
                              static_cast<float>(std::sin(pr)));
    view2_.setToIdentity();
    view2_.lookAt(QVector3D(0, 0, 0), direction, QVector3D(0, 0, 1));
}

void ViewerWidget::setSecondaryPanorama(const cv::Mat& bgr) {
    if (bgr.empty()) return;
    if (!glReady_) {
        pendingSecondary_ = bgr.clone();
        update();
        return;
    }
    makeCurrent();
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    if (!hasTexture2_ || texture2_.width() != rgb.cols ||
        texture2_.height() != rgb.rows) {
        texture2_.destroy();
        texture2_.setSize(rgb.cols, rgb.rows);
        texture2_.setFormat(QOpenGLTexture::RGB8_UNorm);
        texture2_.allocateStorage();
        hasTexture2_ = true;
    }
    texture2_.setData(QOpenGLTexture::RGB, QOpenGLTexture::UInt8, rgb.data);
    texture2_.setMinificationFilter(QOpenGLTexture::Linear);
    texture2_.setMagnificationFilter(QOpenGLTexture::Linear);
    texture2_.setWrapMode(QOpenGLTexture::Repeat);
    doneCurrent();
    update();
}

void ViewerWidget::setSecondaryView(double yawDegrees, double pitchDegrees,
                                    double fovDegrees) {
    yaw2_ = static_cast<float>(yawDegrees);
    pitch2_ = static_cast<float>(std::clamp(pitchDegrees, -89.0, 89.0));
    fov2_ = static_cast<float>(std::clamp(fovDegrees, 15.0, 120.0));
    updateProjection2();
    updateViewMatrix2();
    update();
}

} // namespace ps::gui
