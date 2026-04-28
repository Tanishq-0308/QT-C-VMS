#include "EditSurgeryDialog.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QDebug>

EditSurgeryDialog::EditSurgeryDialog(int surgeryId, QWidget *parent)
    : QDialog(parent), surgeryId(surgeryId) {
    setWindowTitle("Edit Surgery");
        this->setStyleSheet(R"(
            QLineEdit,QTextEdit,QDateEdit {
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

    surgeonNameEdit = new QLineEdit;
    surgeryTypeEdit = new QLineEdit;
    bodyPartEdit = new QLineEdit;
    additionalSurgeonEdit = new QLineEdit;
    anesthesiologistEdit = new QLineEdit;
    surgeryDateEdit = new QDateEdit;
    surgeryDateEdit->setCalendarPopup(true);
    surgeryDateEdit->setDisplayFormat("yyyy-MM-dd");
    operationTheatreEdit = new QLineEdit;
    surgicalHistoryEdit = new QTextEdit;

    formLayout->addRow("Surgeon Name *:", surgeonNameEdit);
    formLayout->addRow("Surgery Type *:", surgeryTypeEdit);
    formLayout->addRow("Body Part:", bodyPartEdit);
    formLayout->addRow("Additional Surgeon:", additionalSurgeonEdit);
    formLayout->addRow("Anesthesiologist:", anesthesiologistEdit);
    formLayout->addRow("Surgery Date *:", surgeryDateEdit);
    formLayout->addRow("Operation Theatre:", operationTheatreEdit);
    formLayout->addRow("Surgical History:", surgicalHistoryEdit);

    mainLayout->addLayout(formLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    saveButton = new QPushButton("Save");
    cancelButton = new QPushButton("Cancel");
    btnLayout->addStretch();
    btnLayout->addWidget(saveButton);
    btnLayout->addWidget(cancelButton);
    mainLayout->addLayout(btnLayout);

    connect(saveButton, &QPushButton::clicked, this, &EditSurgeryDialog::onSaveClicked);
    connect(cancelButton, &QPushButton::clicked, this, &EditSurgeryDialog::reject);

    loadSurgeryData(surgeryId);
}

void EditSurgeryDialog::loadSurgeryData(int surgeryId) {
    QSqlQuery query;
    query.prepare(R"(SELECT surgeon_name, surgery_type, body_part, additional_surgeon,
                           anesthesiologist, surgery_date, operation_theatre_number, surgical_history
                    FROM surgeries WHERE id = ?)");
    query.addBindValue(surgeryId);

    if (query.exec() && query.next()) {
        surgeonNameEdit->setText(query.value(0).toString());
        surgeryTypeEdit->setText(query.value(1).toString());
        bodyPartEdit->setText(query.value(2).toString());
        additionalSurgeonEdit->setText(query.value(3).toString());
        anesthesiologistEdit->setText(query.value(4).toString());
        surgeryDateEdit->setDate(QDate::fromString(query.value(5).toString(), "yyyy-MM-dd"));
        operationTheatreEdit->setText(query.value(6).toString());
        surgicalHistoryEdit->setText(query.value(7).toString());
    } else {
        QMessageBox::warning(this, "Error", "Failed to load surgery data.");
        qDebug() << "Load Error:" << query.lastError().text();
        reject();
    }
}

void EditSurgeryDialog::onSaveClicked() {
    QSqlQuery query;
    query.prepare(R"(UPDATE surgeries SET 
                        surgeon_name = ?, surgery_type = ?, body_part = ?, additional_surgeon = ?, 
                        anesthesiologist = ?, surgery_date = ?, operation_theatre_number = ?, 
                        surgical_history = ? WHERE id = ?)");

    query.addBindValue(surgeonNameEdit->text());
    query.addBindValue(surgeryTypeEdit->text());
    query.addBindValue(bodyPartEdit->text());
    query.addBindValue(additionalSurgeonEdit->text());
    query.addBindValue(anesthesiologistEdit->text());
    query.addBindValue(surgeryDateEdit->date().toString("yyyy-MM-dd"));
    query.addBindValue(operationTheatreEdit->text());
    query.addBindValue(surgicalHistoryEdit->toPlainText());
    query.addBindValue(surgeryId); // make sure this is valid!

    qDebug() << "Attempting to update surgery with ID:" << surgeryId;

    if (!query.exec()) {
        QMessageBox::critical(this, "Database Error", query.lastError().text());
        return;
    }

    QMessageBox::information(this, "Success", "Surgery updated successfully.");
    accept();
}

