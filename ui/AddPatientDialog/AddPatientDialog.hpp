// #ifndef ADDPATIENTDIALOG_HPP
// #define ADDPATIENTDIALOG_HPP

// #include <QDialog>
// #include <QLineEdit>
// #include <QComboBox>
// #include <QDateEdit>
// #include <QTextEdit>
// #include <QPushButton>
// #include <QNetworkAccessManager>

// class AddPatientDialog : public QDialog {
//     Q_OBJECT

// public:
//     AddPatientDialog(QWidget *parent = nullptr);

// private slots:
//     void onAddClicked();
//     void onCancelClicked();
//     void handleNetworkReply(QNetworkReply *reply);

// private:
//     QLineEdit *patientIdInput;
//     QLineEdit *firstNameInput;
//     QLineEdit *lastNameInput;
//     QDateEdit *dobInput;
//     QComboBox *genderInput;
//     QLineEdit *mobileInput;
//     QTextEdit *addressInput;
//     QPushButton *addButton;
//     QPushButton *cancelButton;

//     QNetworkAccessManager *networkManager;
// };

// #endif // ADDPATIENTDIALOG_HPP
// AddPatientDialog.hpp
// AddPatientDialog.hpp
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QPushButton>

class AddPatientDialog : public QDialog {
    Q_OBJECT

    public:
    explicit AddPatientDialog(QWidget *parent = nullptr);
    void loadPatientById(int id);  // ✏️ Edit support

private slots:
    void submitPatient();

private:
    int patientId = -1;  // 🔐 Track if editing (set on loadPatientById)

    QLineEdit *firstNameEdit;
    QLineEdit *lastNameEdit;
    QLineEdit *patientIdEdit;
    QLineEdit *mobileNumberEdit;
    QTextEdit  *addressEdit;
    QDateEdit *dobEdit;
    QComboBox *genderCombo;
    QPushButton *submitButton;
    QPushButton *cancelButton;

};

