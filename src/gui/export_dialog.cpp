#include "gui/export_dialog.hpp"

#include <algorithm>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ps::gui {

ExportDialog::ExportDialog(double durationSeconds, double sourceFps,
                           const QString& defaultOutput, QWidget* parent)
    : QDialog(parent), sourceFps_(sourceFps > 0 ? sourceFps : 25.0) {
    setWindowTitle("Export stitched video");
    setMinimumWidth(480);

    widthSpin_ = new QSpinBox(this);
    widthSpin_->setRange(512, 16384);
    widthSpin_->setSingleStep(512);
    widthSpin_->setValue(2048);
    widthSpin_->setSuffix(" px");

    stepSpin_ = new QSpinBox(this);
    stepSpin_->setRange(1, 500);
    stepSpin_->setValue(1);
    stepSpin_->setSuffix(" source frames");

    fpsSpin_ = new QDoubleSpinBox(this);
    fpsSpin_->setRange(0.01, 120.0);
    fpsSpin_->setValue(sourceFps > 0 ? sourceFps : 25.0);
    fpsSpin_->setSuffix(" fps");
    fpsSpin_->setReadOnly(true);

    startSpin_ = new QDoubleSpinBox(this);
    startSpin_->setRange(0.0, std::max(0.0, durationSeconds));
    startSpin_->setDecimals(2);
    startSpin_->setSuffix(" s");
    startSpin_->setValue(0.0);

    endSpin_ = new QDoubleSpinBox(this);
    endSpin_->setRange(0.0, std::max(0.0, durationSeconds));
    endSpin_->setDecimals(2);
    endSpin_->setSuffix(" s (0 = end of file)");
    endSpin_->setValue(0.0);

    outputEdit_ = new QLineEdit(defaultOutput, this);
    outputEdit_->setReadOnly(true);
    auto* chooseButton = new QPushButton("Choose output file", this);
    connect(chooseButton, &QPushButton::clicked, this, &ExportDialog::chooseOutput);

    auto* form = new QFormLayout();
    form->addRow(new QLabel("Panorama resolution"), widthSpin_);
    form->addRow(new QLabel("Source frame step"), stepSpin_);
    form->addRow(new QLabel("Export frame rate"), fpsSpin_);
    form->addRow(new QLabel("Start at"), startSpin_);
    form->addRow(new QLabel("End at"), endSpin_);
    form->addRow(new QLabel("Output file"), outputEdit_);
    form->addRow(chooseButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

ExportDialogOptions ExportDialog::options() const {
    ExportDialogOptions out;
    out.width = widthSpin_->value();
    out.fps = fpsSpin_->value();
    out.startTime = startSpin_->value();
    out.endTime = endSpin_->value();
    out.output = outputEdit_->text();
    out.everySeconds = stepSpin_->value() / sourceFps_;
    out.fps = sourceFps_ / static_cast<double>(std::max(1, stepSpin_->value()));
    return out;
}

void ExportDialog::chooseOutput() {
    const QString path = QFileDialog::getSaveFileName(
        this, "Export video", outputEdit_->text(), "MP4 (*.mp4)");
    if (!path.isEmpty()) outputEdit_->setText(path);
}

} // namespace ps::gui
