#include "gui/preview_range_dialog.hpp"

#include <QColor>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QRadioButton>
#include <QTimeEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace ps::gui {
namespace {

QTime toTime(double seconds) {
    return QTime(0, 0).addMSecs(static_cast<int>(std::lround(seconds * 1000.0)));
}

double toSeconds(const QTime& time) {
    return time.hour() * 3600.0 + time.minute() * 60.0 + time.second() +
           time.msec() / 1000.0;
}

QString formatTime(double seconds) {
    const int total = static_cast<int>(std::lround(seconds));
    return QString("%1:%2").arg(total / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

} // namespace

RangeSlider::RangeSlider(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(40);
    setMinimumWidth(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void RangeSlider::setMaximumSeconds(double seconds) {
    maximum_ = std::max(0.1, seconds);
    start_ = 0.0;
    end_ = maximum_;
    update();
}

void RangeSlider::setValues(double start, double end) {
    const double gap = std::min(0.2, maximum_ / 10.0);
    start_ = std::clamp(start, 0.0, maximum_ - gap);
    end_ = std::clamp(end, start_ + gap, maximum_);
    update();
}

int RangeSlider::toPixel(double seconds) const {
    const int usable = width() - 16;
    if (usable <= 0) return 8;
    return 8 + static_cast<int>(seconds / maximum_ * usable);
}

double RangeSlider::toSeconds(int x) const {
    const int usable = width() - 16;
    if (usable <= 0) return 0.0;
    return std::clamp((x - 8) / static_cast<double>(usable) * maximum_, 0.0,
                      maximum_);
}

void RangeSlider::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int y = height() / 2;
    const int left = 8;
    const int right = width() - 8;
    if (right <= left) return;

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Mid));
    painter.drawRoundedRect(left, y - 2, right - left, 4, 2, 2);

    const int a = toPixel(start_);
    const int b = toPixel(end_);
    painter.setBrush(palette().color(QPalette::Highlight));
    painter.drawRoundedRect(a, y - 3, std::max(2, b - a), 6, 3, 3);

    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.setBrush(QColor("#e8e8e8"));
    painter.drawRoundedRect(a - 5, y - 10, 10, 20, 3, 3);
    painter.drawRoundedRect(b - 5, y - 10, 10, 20, 3, 3);
}

void RangeSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int x = static_cast<int>(event->position().x());
    dragging_ = std::abs(x - toPixel(start_)) <= std::abs(x - toPixel(end_))
                    ? 1
                    : 2;
    mouseMoveEvent(event);
}

void RangeSlider::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_ || !(event->buttons() & Qt::LeftButton)) return;
    const double gap = std::min(0.2, maximum_ / 10.0);
    const double value = toSeconds(static_cast<int>(event->position().x()));
    if (dragging_ == 1)
        start_ = std::clamp(value, 0.0, end_ - gap);
    else
        end_ = std::clamp(value, start_ + gap, maximum_);
    update();
    emit valuesChanged(start_, end_);
}

PreviewRangeDialog::PreviewRangeDialog(double duration, QWidget* parent)
    : QDialog(parent), duration_(std::max(0.1, duration)) {
    setWindowTitle("Video preview range");
    resize(560, 210);

    all_ = new QRadioButton("Preview the whole video", this);
    range_ = new QRadioButton("Preview only the selected range", this);
    all_->setChecked(true);

    slider_ = new RangeSlider(this);
    slider_->setMaximumSeconds(duration_);
    start_ = 0.0;
    end_ = duration_;
    slider_->setValues(start_, end_);

    startEdit_ = new QTimeEdit(this);
    endEdit_ = new QTimeEdit(this);
    for (QTimeEdit* edit : {startEdit_, endEdit_}) {
        edit->setDisplayFormat("mm:ss");
        edit->setTimeRange(QTime(0, 0), toTime(duration_));
    }
    startEdit_->setTime(toTime(start_));
    endEdit_->setTime(toTime(end_));

    summary_ = new QLabel(this);
    auto* times = new QHBoxLayout();
    times->addWidget(new QLabel("From", this));
    times->addWidget(startEdit_);
    times->addStretch(1);
    times->addWidget(new QLabel("To", this));
    times->addWidget(endEdit_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(all_, &QRadioButton::toggled, this, &PreviewRangeDialog::onModeChanged);
    connect(slider_, &RangeSlider::valuesChanged, this,
            &PreviewRangeDialog::onSliderChanged);
    connect(startEdit_, &QTimeEdit::timeChanged, this,
            &PreviewRangeDialog::onStartEdited);
    connect(endEdit_, &QTimeEdit::timeChanged, this,
            &PreviewRangeDialog::onEndEdited);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(all_);
    layout->addWidget(range_);
    layout->addWidget(slider_);
    layout->addLayout(times);
    layout->addWidget(summary_);
    layout->addWidget(buttons);

    onModeChanged();
    updateSummary();
}

bool PreviewRangeDialog::wholeVideo() const { return all_->isChecked(); }

double PreviewRangeDialog::startSeconds() const { return all_->isChecked() ? 0.0 : start_; }

double PreviewRangeDialog::endSeconds() const {
    return all_->isChecked() ? duration_ : end_;
}

void PreviewRangeDialog::onModeChanged() {
    const bool custom = range_->isChecked();
    slider_->setEnabled(custom);
    startEdit_->setEnabled(custom);
    endEdit_->setEnabled(custom);
    updateSummary();
}

void PreviewRangeDialog::updateSummary() {
    summary_->setText(QString("Selected: %1 to %2 (%3 s)")
                          .arg(formatTime(startSeconds()))
                          .arg(formatTime(endSeconds()))
                          .arg(endSeconds() - startSeconds(), 0, 'f', 1));
}

void PreviewRangeDialog::onSliderChanged(double start, double end) {
    if (updating_) return;
    updating_ = true;
    start_ = start;
    end_ = end;
    startEdit_->setTime(toTime(start_));
    endEdit_->setTime(toTime(end_));
    updating_ = false;
    updateSummary();
}

void PreviewRangeDialog::onStartEdited(const QTime& time) {
    if (updating_) return;
    const double gap = std::min(0.2, duration_ / 10.0);
    start_ = std::clamp(toSeconds(time), 0.0, end_ - gap);
    updating_ = true;
    startEdit_->setTime(toTime(start_));
    updating_ = false;
    slider_->setValues(start_, end_);
    updateSummary();
}

void PreviewRangeDialog::onEndEdited(const QTime& time) {
    if (updating_) return;
    const double gap = std::min(0.2, duration_ / 10.0);
    end_ = std::clamp(toSeconds(time), start_ + gap, duration_);
    updating_ = true;
    endEdit_->setTime(toTime(end_));
    updating_ = false;
    slider_->setValues(start_, end_);
    updateSummary();
}

} // namespace ps::gui
