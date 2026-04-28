#ifndef PATIENTPAGE_HPP
#define PATIENTPAGE_HPP

#include "core/ResponsiveWidget.hpp"
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonArray>

class PatientPage : public ResponsiveWidget {
    Q_OBJECT

public:
    explicit PatientPage(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    void updateScaling() override;

signals:
    void addPatientRequested();
    void viewSurgeryDetailsRequested(const QString &patientId);

private slots:
    void onAddPatientClicked();
    void onRowDoubleClicked(int row, int column);
    void onSearchTextChanged(const QString &text);
    void loadPatientsFromDatabase();

private:
    void setupUI();
    void applyStyles();

    QTableWidget *patientTable;
    QLineEdit *searchBar;
    QPushButton *addButton;
    
    bool m_stylesApplied = false;
};

#endif // PATIENTPAGE_HPP