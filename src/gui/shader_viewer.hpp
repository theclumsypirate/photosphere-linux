#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

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
    void resetView();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateUniforms();

    QOpenGLShaderProgram program_;
    QOpenGLTexture texture_{QOpenGLTexture::Target2D};
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
