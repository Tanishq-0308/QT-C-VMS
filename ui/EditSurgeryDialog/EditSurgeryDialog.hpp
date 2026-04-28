#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateEdit>
#include <QPushButton>

class EditSurgeryDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditSurgeryDialog(int surgeryId, QWidget *parent = nullptr);

private slots:
    void onSaveClicked();

private:
    void loadSurgeryData(int surgeryId);

    int surgeryId;
    QLineEdit *surgeonNameEdit;
    QLineEdit *surgeryTypeEdit;
    QLineEdit *bodyPartEdit;
    QLineEdit *additionalSurgeonEdit;
    QLineEdit *anesthesiologistEdit;
    QDateEdit *surgeryDateEdit;
    QLineEdit *operationTheatreEdit;
    QTextEdit *surgicalHistoryEdit;
    QPushButton *saveButton;
    QPushButton *cancelButton;
};
