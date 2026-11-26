#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QMatrix4x4>
#include <QPixmap>
#include <QPushButton>

#include <opencv2/core.hpp>

#include <vector>

namespace ps::gui {

// Equirectangular sphere viewer, modeled on result/PanoramaView.kt.
// Drag to look around, wheel to change the field of view.
class ViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit ViewerWidget(QWidget* parent = nullptr);
    ~ViewerWidget() override;

    void setPanorama(const cv::Mat& bgr);

    // Captures exactly what is currently on screen, including the rotation.
    QPixmap currentView();

signals:
    void snapshotRequested();

public slots:
    void resetView();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildSphere();
    void updateViewMatrix();

    QOpenGLShaderProgram program_;
    QOpenGLTexture texture_{QOpenGLTexture::Target2D};
    QMatrix4x4 projection_;
    QMatrix4x4 view_;
    std::vector<float> vertices_;
    std::vector<unsigned int> indices_;
    unsigned int vbo_ = 0;
    unsigned int ibo_ = 0;
    QOpenGLVertexArrayObject vao_;
    QPoint lastPos_;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float fov_ = 75.0f;
    bool dragging_ = false;
    bool hasTexture_ = false;
    QPushButton* snapshotButton_ = nullptr;
};

} // namespace ps::gui
