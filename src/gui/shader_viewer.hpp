#pragma once

#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

#include <opencv2/core.hpp>

namespace ps::gui {

// Real-time dual-fisheye stitch on the GPU. The input is one side-by-side
// frame; a fragment shader projects each output ray into both lenses and
// blends them across the seam. Drag to look around, wheel to zoom.
class ShaderViewer : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit ShaderViewer(QWidget* parent = nullptr);
    ~ShaderViewer() override;

public slots:
    void setFrame(const cv::Mat& sideBySide);
    void setLensFov(double degrees) { lensFov_ = static_cast<float>(degrees); }
    void resetView();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateUniforms();
    void uploadFrame();

    bool glReady_ = false;
    QImage pendingImage_;
    QOpenGLShaderProgram program_;
    QVector<QOpenGLTexture*> textures_;
    QOpenGLVertexArrayObject vao_;
    unsigned int vbo_ = 0;
    QPoint lastPos_;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float fov_ = 90.0f;
    float lensFov_ = 195.0f;
    bool hasFrame_ = false;
};

} // namespace ps::gui
