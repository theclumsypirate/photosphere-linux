#include "gui/save_format_dialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace ps::gui {

SaveFormatDialog::SaveFormatDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Save panorama");
    jpeg_ = new QRadioButton("JPEG, 8-bit (simple)", this);
    exr_ = new QRadioButton("OpenEXR, 32-bit float HDR (from DNG brackets)", this);
    jpeg_->setChecked(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Choose output format", this));
    layout->addWidget(jpeg_);
    layout->addWidget(exr_);
    layout->addWidget(buttons);
}

SaveFormat SaveFormatDialog::format() const {
    return exr_->isChecked() ? SaveFormat::Exr : SaveFormat::Jpeg;
}

} // namespace ps::gui
