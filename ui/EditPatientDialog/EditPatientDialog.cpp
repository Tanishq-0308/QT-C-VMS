#include "EditPatientDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QDebug>

EditPatientDialog::EditPatientDialog(const QString &patientId, QWidget *parent)
    : QDialog(parent), patientId(patientId) {
    setWindowTitle("Edit Patient");
    setModal(true);
    this->setStyleSheet(R"(
        QLineEdit,QTextEdit,QDateEdit,QComboBox {
            height: 24px;
            font-size:24px;
        }
        QPushButton {
            font-size:20px;
        }
        )");
    setFixedSize(700, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *formLayout = new QFormLayout;

    patientIdEdit = new QLineEdit;
    patientIdEdit->setReadOnly(true);
    formLayout->addRow("Patient Id:", patientIdEdit);

    firstNameEdit = new QLineEdit;
    formLayout->addRow("First Name *:", firstNameEdit);

    lastNameEdit = new QLineEdit;
    formLayout->addRow("Last Name *:", lastNameEdit);

    dobEdit = new QDateEdit;
    dobEdit->setCalendarPopup(true);
    dobEdit->setDisplayFormat("yyyy-MM-dd");
    formLayout->addRow("Date Of Birth *:", dobEdit);

    genderCombo = new QComboBox;
    genderCombo->addItems({"Male", "Female", "Other"});
    formLayout->addRow("Gender *:", genderCombo);

    mobileEdit = new QLineEdit;
    formLayout->addRow("Mobile Number *:", mobileEdit);

    addressEdit = new QTextEdit;
    formLayout->addRow("Address *:", addressEdit);

    mainLayout->addLayout(formLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    saveButton = new QPushButton("Save");
    cancelButton = new QPushButton("Cancel");

    connect(saveButton, &QPushButton::clicked, this, &EditPatientDialog::onSaveClicked);
    connect(cancelButton, &QPushButton::clicked, this, &EditPatientDialog::reject);

    btnLayout->addStretch();
    btnLayout->addWidget(saveButton);
    btnLayout->addWidget(cancelButton);
    mainLayout->addLayout(btnLayout);

    loadPatientData(patientId);
}

void EditPatientDialog::loadPatientData(const QString &patientId) {
    QSqlQuery query;
    query.prepare("SELECT patient_id, first_name, last_name, dob, gender, mobile, address FROM patients WHERE patient_id = ?");
    query.addBindValue(patientId);

    if (query.exec() && query.next()) {
        patientIdEdit->setText(query.value(0).toString());
        firstNameEdit->setText(query.value(1).toString());
        lastNameEdit->setText(query.value(2).toString());
        dobEdit->setDate(QDate::fromString(query.value(3).toString(), "yyyy-MM-dd"));
        genderCombo->setCurrentText(query.value(4).toString());
        mobileEdit->setText(query.value(5).toString());
        addressEdit->setText(query.value(6).toString());
    } else {
        QMessageBox::warning(this, "Error", "Failed to load patient data.");
        qDebug() << "DB Error:" << query.lastError().text();
        reject();
    }
}

void EditPatientDialog::onSaveClicked() {
    QSqlQuery query;
    query.prepare(R"(
        UPDATE patients SET 
            first_name = ?, 
            last_name = ?, 
            dob = ?, 
            gender = ?, 
            mobile = ?, 
            address = ?
        WHERE patient_id = ?
    )");

    query.addBindValue(firstNameEdit->text());
    query.addBindValue(lastNameEdit->text());
    query.addBindValue(dobEdit->date().toString("yyyy-MM-dd"));
    query.addBindValue(genderCombo->currentText());
    query.addBindValue(mobileEdit->text());
    query.addBindValue(addressEdit->toPlainText());
    query.addBindValue(patientId);

    if (query.exec()) {
        QMessageBox::information(this, "Success", "Patient updated successfully.");
        accept();
    } else {
        QMessageBox::warning(this, "Error", "Failed to update patient.");
        qDebug() << "Update Error:" << query.lastError().text();
         emit patientUpdated(); 
    }
}
