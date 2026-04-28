// // #include "AddPatientDialog.hpp"
// // #include <QFormLayout>
// // #include <QHBoxLayout>
// // #include <QVBoxLayout>
// // #include <QLabel>
// // #include <QFile>
// // #include <QJsonDocument>
// // #include <QJsonObject>
// // #include <QNetworkRequest>
// // #include <QNetworkReply>
// // #include <QMessageBox>

// // AddPatientDialog::AddPatientDialog(QWidget *parent) : QDialog(parent) {
// //         QFile qss("assets/styles/AddPatient.qss");
// //     if (qss.open(QFile::ReadOnly | QFile::Text)) {
// //         QString style = qss.readAll();
// //         this->setStyleSheet(style);
// //     }
// //     setWindowTitle("Add Patient");
// //     setFixedSize(400, 500);

// //     patientIdInput = new QLineEdit();
// //     firstNameInput = new QLineEdit();
// //     lastNameInput = new QLineEdit();
// //     dobInput = new QDateEdit();
// //     dobInput->setDisplayFormat("dd/MM/yyyy");
// //     dobInput->setCalendarPopup(true);
// //     genderInput = new QComboBox();
// //     genderInput->addItems({"Select Gender", "Male", "Female", "Other"});
// //     mobileInput = new QLineEdit();
// //     addressInput = new QTextEdit();

// //     addButton = new QPushButton("Add");
// //     cancelButton = new QPushButton("Cancel");

// //     QFormLayout *formLayout = new QFormLayout();
// //     formLayout->addRow("Hospital Patient Id", patientIdInput);
// //     formLayout->addRow("First Name *", firstNameInput);
// //     formLayout->addRow("Last Name *", lastNameInput);
// //     formLayout->addRow("Date of Birth *", dobInput);
// //     formLayout->addRow("Gender *", genderInput);
// //     formLayout->addRow("Mobile Number *", mobileInput);
// //     formLayout->addRow("Address *", addressInput);

// //     QHBoxLayout *buttonLayout = new QHBoxLayout();
// //     buttonLayout->addStretch();
// //     buttonLayout->addWidget(addButton);
// //     buttonLayout->addWidget(cancelButton);

// //     QVBoxLayout *mainLayout = new QVBoxLayout(this);
// //     mainLayout->addLayout(formLayout);
// //     mainLayout->addLayout(buttonLayout);

// //     connect(addButton, &QPushButton::clicked, this, &AddPatientDialog::onAddClicked);
// //     connect(cancelButton, &QPushButton::clicked, this, &AddPatientDialog::onCancelClicked);

// //     networkManager = new QNetworkAccessManager(this);
// //     connect(networkManager, &QNetworkAccessManager::finished, this, &AddPatientDialog::handleNetworkReply);
// // }

// // void AddPatientDialog::onAddClicked() {
// //     if (firstNameInput->text().isEmpty() || lastNameInput->text().isEmpty() || 
// //         mobileInput->text().isEmpty() || addressInput->toPlainText().isEmpty() || 
// //         genderInput->currentIndex() == 0) {
// //         QMessageBox::warning(this, "Validation Error", "Please fill in all required fields.");
// //         return;
// //     }

// //     QJsonObject patientData;
// //     patientData["patient_id"] = patientIdInput->text();
// //     patientData["first_name"] = firstNameInput->text();
// //     patientData["last_name"] = lastNameInput->text();
// //     patientData["dob"] = dobInput->date().toString("yyyy-MM-dd");
// //     patientData["gender"] = genderInput->currentText();
// //     patientData["mobile"] = mobileInput->text();
// //     patientData["address"] = addressInput->toPlainText();

// //     QNetworkRequest request(QUrl("http://localhost:8000/api/patients/add/"));  // Change to your API
// //     request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
// //     request.setRawHeader("Authorization", "Bearer YOUR_JWT_TOKEN");  // Replace with real token

// //     networkManager->post(request, QJsonDocument(patientData).toJson());
// // }

// // void AddPatientDialog::onCancelClicked() {
// //     reject();
// // }

// // void AddPatientDialog::handleNetworkReply(QNetworkReply *reply) {
// //     if (reply->error() == QNetworkReply::NoError) {
// //         QMessageBox::information(this, "Success", "Patient added successfully!");
// //         accept();
// //     } else {
// //         QMessageBox::critical(this, "Error", reply->errorString());
// //     }
// //     reply->deleteLater();
// // }

// // AddPatientDialog.cpp
// // AddPatientDialog.cpp
// #include "AddPatientDialog.hpp"
// #include <QVBoxLayout>
// #include <QFormLayout>
// #include <QHBoxLayout>
// #include <QJsonObject>
// #include <QJsonDocument>
// #include <QNetworkRequest>
// #include <QNetworkReply>
// #include <QDebug>

// AddPatientDialog::AddPatientDialog(QWidget *parent) : QDialog(parent) {
//     setWindowTitle("Add Patient");
//     setFixedWidth(400);

//     QVBoxLayout *layout = new QVBoxLayout(this);
//     QFormLayout *formLayout = new QFormLayout;

//     patientIdEdit = new QLineEdit(this);
//     firstNameEdit = new QLineEdit(this);
//     lastNameEdit = new QLineEdit(this);
//     dobEdit = new QDateEdit(QDate::currentDate(), this);
//     dobEdit->setCalendarPopup(true);
//     dobEdit->setDisplayFormat("dd/MM/yyyy");

//     genderCombo = new QComboBox(this);
//     genderCombo->addItem("Select Gender");
//     genderCombo->addItems({"Male", "Female", "Other"});

//     mobileNumberEdit = new QLineEdit(this);
//     mobileNumberEdit->setPlaceholderText("9999999999");

//     addressEdit = new QTextEdit(this);
//     addressEdit->setPlaceholderText("Patient's Residential Address");
//     addressEdit->setFixedHeight(50);

//     formLayout->addRow("Hospital Patient Id", patientIdEdit);
//     formLayout->addRow("First Name *", firstNameEdit);
//     formLayout->addRow("Last Name *", lastNameEdit);
//     formLayout->addRow("Date of Birth *", dobEdit);
//     formLayout->addRow("Gender *", genderCombo);
//     formLayout->addRow("Mobile Number *", mobileNumberEdit);
//     formLayout->addRow("Address *", addressEdit);

//     layout->addLayout(formLayout);

//     QHBoxLayout *buttonLayout = new QHBoxLayout;
//     submitButton = new QPushButton("Add", this);
//     cancelButton = new QPushButton("Cancel", this);
//     buttonLayout->addStretch();
//     buttonLayout->addWidget(submitButton);
//     buttonLayout->addWidget(cancelButton);

//     layout->addLayout(buttonLayout);

//     networkManager = new QNetworkAccessManager(this);

//     connect(submitButton, &QPushButton::clicked, this, &AddPatientDialog::submitPatient);
//     connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
// }

// void AddPatientDialog::submitPatient() {
//     QJsonObject payload;
//     payload["hospital_patient_id"] = patientIdEdit->text();
//     payload["first_name"] = firstNameEdit->text();
//     payload["last_name"] = lastNameEdit->text();
//     payload["date_of_birth"] = dobEdit->date().toString("yyyy-MM-dd");
//     payload["gender"] = genderCombo->currentText();
//     payload["mobile_number"] = mobileNumberEdit->text();
//     payload["address"] = addressEdit->toPlainText();

//     QNetworkRequest request(QUrl("http://localhost:8000/api/patients/add-patient/"));
//     request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
//     request.setRawHeader("Authorization", "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ0b2tlbl90eXBlIjoiYWNjZXNzIiwiZXhwIjoxNzQ4MDk2NTI4LCJpYXQiOjE3NDgwMTAxMjgsImp0aSI6ImVkNWJhOGU3YWE3MjQ2YTJhYWI4YjllNTI4ZmEzNjkzIiwidXNlcl9pZCI6ImJyYWluIn0.Ghcl6VfrCPFNClYBLzBeoiBbKBURdajtEfPzGG0apZA");

//     QNetworkReply *reply = networkManager->post(request, QJsonDocument(payload).toJson());
//     connect(reply, &QNetworkReply::finished, [=]() {
//         if (reply->error() == QNetworkReply::NoError) {
//             accept();  // Close dialog
//         } else {
//             qWarning() << "Failed to add patient:" << reply->errorString();
//         }
//         reply->deleteLater();
//     });
// }

#include "AddPatientDialog.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDate>
#include <QMessageBox>


AddPatientDialog::AddPatientDialog(QWidget *parent) : QDialog(parent), patientId(-1) {
    this->setObjectName("AddPatientDialogPage");
    this->setWindowTitle("Add New Patient");
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

    auto *mainLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout();

    firstNameEdit = new QLineEdit();
    lastNameEdit = new QLineEdit();
    patientIdEdit = new QLineEdit();
    mobileNumberEdit = new QLineEdit();
    addressEdit = new QTextEdit();
    dobEdit = new QDateEdit();
    genderCombo = new QComboBox();

    genderCombo->addItems({"Male", "Female", "Other"});
    dobEdit->setCalendarPopup(true);
    dobEdit->setDisplayFormat("yyyy-MM-dd");
    dobEdit->setDate(QDate::currentDate());

    formLayout->addRow("First Name:", firstNameEdit);
    formLayout->addRow("Last Name:", lastNameEdit);
    formLayout->addRow("Patient ID:", patientIdEdit);
    formLayout->addRow("Mobile Number:", mobileNumberEdit);
    formLayout->addRow("Date of Birth:", dobEdit);
    formLayout->addRow("Gender:", genderCombo);
    formLayout->addRow("Address:", addressEdit);

    auto *btnLayout = new QHBoxLayout();
    submitButton = new QPushButton("Submit");
    cancelButton = new QPushButton("Cancel");
    btnLayout->addStretch();
    btnLayout->addWidget(submitButton);
    btnLayout->addWidget(cancelButton);

    mainLayout->addLayout(formLayout);
    mainLayout->addLayout(btnLayout);

    // networkManager = new QNetworkAccessManager(this);

    connect(submitButton, &QPushButton::clicked, this, &AddPatientDialog::submitPatient);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::close);
}

void AddPatientDialog::loadPatientById(int id) {
    QSqlQuery query;
    query.prepare("SELECT first_name, last_name, patient_id, mobile, dob, gender, address FROM patients WHERE id = ?");
    query.addBindValue(id);

    if (query.exec() && query.next()) {
        firstNameEdit->setText(query.value(0).toString());
        lastNameEdit->setText(query.value(1).toString());
        patientIdEdit->setText(query.value(2).toString());
        mobileNumberEdit->setText(query.value(3).toString());
        dobEdit->setDate(QDate::fromString(query.value(4).toString(), "yyyy-MM-dd"));
        genderCombo->setCurrentText(query.value(5).toString());
        addressEdit->setPlainText(query.value(6).toString());

        patientId = id;
        setWindowTitle("Edit Patient");
    } else {
        qWarning() << "❌ Failed to load patient:" << query.lastError().text();
    }
}
void AddPatientDialog::submitPatient() {
    QString first = firstNameEdit->text().trimmed();
    QString last = lastNameEdit->text().trimmed();
    QString pid = patientIdEdit->text().trimmed();
    QString mobile = mobileNumberEdit->text().trimmed();
    QString dob = dobEdit->date().toString("yyyy-MM-dd");
    QString gender = genderCombo->currentText();
    QString address = addressEdit->toPlainText().trimmed();

    // 🛑 Validate required fields
    if (first.isEmpty() || last.isEmpty() || pid.isEmpty()) {
        qWarning() << "⚠️ Validation failed: Required fields are empty";
        QMessageBox::warning(this, "Missing Information", "Please fill in First Name, Last Name, and Patient ID.");
        return;
    }

    // 🔍 Check for duplicate patient_id
    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT COUNT(*) FROM patients WHERE patient_id = ? AND id != ?");
    checkQuery.addBindValue(pid);
    checkQuery.addBindValue(patientId > 0 ? patientId : -1);  // Exclude current row in edit mode

    if (!checkQuery.exec()) {
        qWarning() << "❌ Failed to check for duplicate patient_id:" << checkQuery.lastError().text();
        QMessageBox::critical(this, "Database Error", "Could not validate patient ID uniqueness.");
        return;
    }

    if (checkQuery.next() && checkQuery.value(0).toInt() > 0) {
        QMessageBox::warning(this, "Duplicate Patient ID", "This Patient ID is already in use. Please enter a unique ID.");
        return;
    }

    // 🗃️ Save data
    QSqlQuery query;

    if (patientId > 0) {
        // ✏️ Update
        query.prepare(R"(
            UPDATE patients 
            SET first_name=?, last_name=?, patient_id=?, mobile=?, dob=?, gender=?, address=? 
            WHERE id=?)");
        query.addBindValue(first);
        query.addBindValue(last);
        query.addBindValue(pid);
        query.addBindValue(mobile);
        query.addBindValue(dob);
        query.addBindValue(gender);
        query.addBindValue(address);
        query.addBindValue(patientId);
    } else {
        // ➕ Insert
        query.prepare(R"(
            INSERT INTO patients 
            (first_name, last_name, patient_id, mobile, dob, gender, address) 
            VALUES (?, ?, ?, ?, ?, ?, ?))");
        query.addBindValue(first);
        query.addBindValue(last);
        query.addBindValue(pid);
        query.addBindValue(mobile);
        query.addBindValue(dob);
        query.addBindValue(gender);
        query.addBindValue(address);
    }

    if (!query.exec()) {
        qWarning() << "❌ Failed to save patient:" << query.lastError().text();
        QMessageBox::critical(this, "Database Error", "Could not save the patient. Please try again.");
        return;
    }

    qDebug() << (patientId > 0 ? "✅ Updated patient" : "✅ Added patient") << pid;
    accept();  // ✅ Close dialog and trigger table refresh
}
