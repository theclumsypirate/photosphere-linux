#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

namespace ps::gui {

// Runs the-tourist-filter on a folder of already-aligned photos.
class TouristFilterDialog : public QDialog {
    Q_OBJECT

public:
    explicit TouristFilterDialog(QWidget* parent = nullptr);
    ~TouristFilterDialog() override;

private slots:
    void chooseFolder();
    void chooseTool();
    void run();
    void cancel();
    void onOutput();
    void onFinished(int exitCode);
    void onError();

private:
    QString toolFolder() const;
    void setRunning(bool running);
    void showResult();

    QLineEdit* folderEdit_ = nullptr;
    QComboBox* methodCombo_ = nullptr;
    QLineEdit* toolEdit_ = nullptr;
    QLineEdit* resultEdit_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QPushButton* runButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QProcess* process_ = nullptr;
};

} // namespace ps::gui
