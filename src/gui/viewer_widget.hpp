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
#include <QSlider>

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
    void setSecondaryPanorama(const cv::Mat& bgr);
    void setView(double yawDegrees, double pitchDegrees, double fovDegrees);
    void setSecondaryView(double yawDegrees, double pitchDegrees, double fovDegrees);
    void setPipEnabled(bool enabled) { pipEnabled_ = enabled; update(); }
    void setOverlayButtonsVisible(bool visible);
    double yaw() const { return yaw_; }
    double pitch() const { return pitch_; }
    double fov() const { return fov_; }
    double yaw2() const { return yaw2_; }
    double pitch2() const { return pitch2_; }
    double fov2() const { return fov2_; }

    // Captures exactly what is currently on screen, including the rotation.
    QPixmap currentView();

signals:
    void snapshotRequested();
    void panoramaSaveRequested();
    void stabilizeRequested();

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
    void updateProjection();
    void updateProjection2();
    void updateViewMatrix();
    void updateViewMatrix2();
    QRect secondaryRect() const;

    QOpenGLShaderProgram program_;
    QOpenGLTexture texture_{QOpenGLTexture::Target2D};
    QOpenGLTexture texture2_{QOpenGLTexture::Target2D};
    QMatrix4x4 projection_;
    QMatrix4x4 projection2_;
    QMatrix4x4 view_;
    QMatrix4x4 view2_;
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
    bool glReady_ = false;
    cv::Mat pendingPanorama_;
    cv::Mat pendingSecondary_;
    float yaw2_ = 0.0f;
    float pitch2_ = 0.0f;
    float fov2_ = 75.0f;
    bool hasTexture2_ = false;
    bool pipEnabled_ = false;
    bool editingSecondary_ = false;
    QPushButton* snapshotButton_ = nullptr;
    QPushButton* savePanoButton_ = nullptr;
    QSlider* fovSlider_ = nullptr;
    QPushButton* stabilizeButton_ = nullptr;
};

} // namespace ps::gui
