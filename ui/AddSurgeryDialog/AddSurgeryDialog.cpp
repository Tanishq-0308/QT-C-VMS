#include "AddSurgeryDialog.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QDebug>

AddSurgeryDialog::AddSurgeryDialog(const QString& patientId, QWidget* parent)
    : QDialog(parent), patientId(patientId)
{
    setWindowTitle("Add Surgery");
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

    auto layout = new QVBoxLayout(this);
    auto form = new QFormLayout();

    surgeonEdit = new QLineEdit();
    surgeryTypeEdit = new QLineEdit();
    bodyPartEdit = new QLineEdit();
    additionalSurgeonEdit = new QLineEdit();
    anesthesiologistEdit = new QLineEdit();
    otNumberEdit = new QLineEdit();
    dateEdit = new QDateEdit(QDate::currentDate());
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");
    surgicalHistoryEdit = new QTextEdit();

    form->addRow("Surgeon Name*", surgeonEdit);
    form->addRow("Surgery Type*", surgeryTypeEdit);
    form->addRow("Body Part", bodyPartEdit);
    form->addRow("Additional Surgeon", additionalSurgeonEdit);
    form->addRow("Anesthesiologist", anesthesiologistEdit);
    form->addRow("Surgery Date*", dateEdit);
    form->addRow("OT Number", otNumberEdit);
    form->addRow("Surgical History", surgicalHistoryEdit);

    layout->addLayout(form);

    auto saveBtn = new QPushButton("Save");
    layout->addWidget(saveBtn);

    connect(saveBtn, &QPushButton::clicked, this, &AddSurgeryDialog::onSaveClicked);
}

void AddSurgeryDialog::onSaveClicked() {
    if (surgeonEdit->text().isEmpty() || surgeryTypeEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Missing Info", "Surgeon Name and Surgery Type are required.");
        return;
    }

    QSqlQuery query;
    query.prepare(R"(
        INSERT INTO surgeries (
            patient_id, surgeon_name, surgery_type, body_part,
            additional_surgeon, anesthesiologist, surgery_date,
            operation_theatre_number, surgical_history
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    query.addBindValue(patientId);
    query.addBindValue(surgeonEdit->text());
    query.addBindValue(surgeryTypeEdit->text());
    query.addBindValue(bodyPartEdit->text());
    query.addBindValue(additionalSurgeonEdit->text());
    query.addBindValue(anesthesiologistEdit->text());
    query.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
    query.addBindValue(otNumberEdit->text());
    query.addBindValue(surgicalHistoryEdit->toPlainText());

    if (!query.exec()) {
        QMessageBox::critical(this, "Error", "Failed to add surgery:\n" + query.lastError().text());
        return;
    }

    emit surgeryAdded();
    accept();
}
