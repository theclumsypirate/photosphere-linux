#pragma once

#include <QDialog>
#include <QTime>
#include <QWidget>

class QLabel;
class QRadioButton;
class QTimeEdit;

namespace ps::gui {

// Horizontal bar with two draggable handles for choosing a time range.
class RangeSlider : public QWidget {
    Q_OBJECT

public:
    explicit RangeSlider(QWidget* parent = nullptr);

    void setMaximumSeconds(double seconds);
    void setValues(double start, double end);
    double startSeconds() const { return start_; }
    double endSeconds() const { return end_; }

signals:
    void valuesChanged(double start, double end);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    int toPixel(double seconds) const;
    double toSeconds(int x) const;

    double maximum_ = 1.0;
    double start_ = 0.0;
    double end_ = 1.0;
    int dragging_ = 0;
};

// Asks whether to preview the whole video or only a chosen range.
class PreviewRangeDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreviewRangeDialog(double duration, QWidget* parent = nullptr);

    bool wholeVideo() const;
    double startSeconds() const;
    double endSeconds() const;

private slots:
    void onModeChanged();
    void onSliderChanged(double start, double end);
    void onStartEdited(const QTime& time);
    void onEndEdited(const QTime& time);
    void updateSummary();

private:
    QRadioButton* all_ = nullptr;
    QRadioButton* range_ = nullptr;
    RangeSlider* slider_ = nullptr;
    QTimeEdit* startEdit_ = nullptr;
    QTimeEdit* endEdit_ = nullptr;
    QLabel* summary_ = nullptr;
    double duration_ = 0.0;
    double start_ = 0.0;
    double end_ = 0.0;
    bool updating_ = false;
};

} // namespace ps::gui
