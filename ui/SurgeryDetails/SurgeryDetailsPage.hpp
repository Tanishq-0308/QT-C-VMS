#ifndef SURGERYDETAILSPAGE_HPP
#define SURGERYDETAILSPAGE_HPP

#include "core/ResponsiveWidget.hpp"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSpacerItem>
#include <QHBoxLayout>
#include <QMap>

class SurgeryDetailsPage : public ResponsiveWidget {
    Q_OBJECT

public:
    explicit SurgeryDetailsPage(QWidget* parent = nullptr);
    void loadPatientData(const QString &patientId);

protected:
    void showEvent(QShowEvent *event) override;
    void updateScaling() override;

signals:
    void openSurgeryRecordingPage(const QString& patientId, int surgeryId);
    void goBackRequested();
    void editPatientRequested();
    void addSurgeryRequested();

private:
    void setupUI();
    void applyStyles();
    QLabel* createLabel(const QString& text, bool bold = false);
    void loadSurgeriesForPatient(const QString& patientId);

    // Widgets
    QPushButton* backButton;
    QLabel* patientTitle;
    QLabel* surgeryTitle;
    QPushButton* editPatientButton;
    QPushButton* newSurgeryButton;
    QTableWidget* surgeryTable;
    QGridLayout* patientGrid;
    
    // Patient data labels
    QMap<QString, QLabel*> valueLabels;
    QMap<QString, QLabel*> keyLabels;
    
    QString currentPatientId;
    bool m_stylesApplied = false;
};

#endif // SURGERYDETAILSPAGE_HPP