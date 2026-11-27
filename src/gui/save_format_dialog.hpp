#pragma once

#include <QDialog>

class QRadioButton;

namespace ps::gui {

enum class SaveFormat { Jpeg, Exr };

class SaveFormatDialog : public QDialog {
    Q_OBJECT

public:
    explicit SaveFormatDialog(QWidget* parent = nullptr);
    SaveFormat format() const;

private:
    QRadioButton* jpeg_ = nullptr;
    QRadioButton* exr_ = nullptr;
};

} // namespace ps::gui
