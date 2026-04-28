// EditPatientDialog.hpp

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QPushButton>

class EditPatientDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditPatientDialog(const QString &patientId, QWidget *parent = nullptr);

signals:
    void patientUpdated();

private slots:
    void onSaveClicked();

private:
    void loadPatientData(const QString &patientId);

    QString patientId;
    QLineEdit *patientIdEdit;
    QLineEdit *firstNameEdit;
    QLineEdit *lastNameEdit;
    QDateEdit *dobEdit;
    QComboBox *genderCombo;
    QLineEdit *mobileEdit;
    QTextEdit *addressEdit;
    QPushButton *saveButton;
    QPushButton *cancelButton;
};
