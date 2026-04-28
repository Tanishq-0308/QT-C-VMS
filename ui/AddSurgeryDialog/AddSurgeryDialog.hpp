#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QDateEdit>
#include <QPushButton>

class AddSurgeryDialog : public QDialog {
    Q_OBJECT

public:
    AddSurgeryDialog(const QString& patientId, QWidget* parent = nullptr);
    QString getSurgeonName() const;
    QString getSurgeryType() const;
    QString getBodyPart() const;
    QString getAdditionalSurgeon() const;
    QString getAnesthesiologist() const;
    QString getSurgeryDate() const;
    QString getOperationTheatreNumber() const;
    QString getSurgicalHistory() const;

signals:
    void surgeryAdded();

private slots:
    void onSaveClicked();

private:
    QString patientId;
    QLineEdit *surgeonEdit, *surgeryTypeEdit, *bodyPartEdit, *additionalSurgeonEdit, *anesthesiologistEdit, *otNumberEdit;
    QTextEdit *surgicalHistoryEdit;
    QDateEdit *dateEdit;
};
