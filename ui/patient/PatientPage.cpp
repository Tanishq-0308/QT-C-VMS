#include "PatientPage.hpp"
#include <QFile>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSqlQuery>
#include <QSqlError>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QDebug>
#include <QTimer>
#include "../AddPatientDialog/AddPatientDialog.hpp"

PatientPage::PatientPage(QWidget *parent) : ResponsiveWidget(parent) {
    setupUI();
    applyStyles();
    // Data will be loaded in showEvent
}

void PatientPage::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Top bar
    QHBoxLayout *topLayout = new QHBoxLayout;
    searchBar = new QLineEdit(this);
    searchBar->setObjectName("searchBar");
    searchBar->setPlaceholderText("Search Patients Here");
    
    addButton = new QPushButton("+ Add Patient", this);
    addButton->setObjectName("addButton");
    addButton->setCursor(Qt::PointingHandCursor);
    
    topLayout->addWidget(searchBar);
    topLayout->addWidget(addButton);

    // Table setup
    patientTable = new QTableWidget(this);
    patientTable->setObjectName("patientTable");
    patientTable->setColumnCount(6);
    patientTable->setHorizontalHeaderLabels({"PATIENT ID", "FIRST NAME", "LAST NAME", "DOB", "GENDER", "SURGERY"});
    patientTable->horizontalHeader()->setStretchLastSection(true);
    patientTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    patientTable->verticalHeader()->setVisible(false);

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(patientTable);
    setLayout(mainLayout);

    connect(addButton, &QPushButton::clicked, this, &PatientPage::onAddPatientClicked);
    connect(searchBar, &QLineEdit::textChanged, this, &PatientPage::onSearchTextChanged);
}

void PatientPage::applyStyles() {
    // Apply stylesheet ONCE - keep original design
    QString style = R"(
        #searchBar {
            font-size: 18px;
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 8px;
            background: #fafafa;
        }
        
        #searchBar:focus {
            border: 2px solid #003366;
            background: white;
        }
        
        #addButton {
            background-color: #003366;
            color: white;
            border: none;
            border-radius: 6px;
            font-weight: bold;
            font-size: 18px;
            padding: 15px 20px;
        }
        
        #addButton:hover {
            background-color: #0059b3;
        }
        
        #patientTable {
            font-size: 18px;
            border: 1px solid #ddd;
            gridline-color: #ddd;
            background-color: white;
        }
        
        #patientTable::item {
            padding: 8px;
        }
        
        #patientTable QHeaderView::section {
            background-color: #003366;
            color: white;
            padding: 12px;
            border: none;
            font-weight: bold;
            font-size: 16px;
        }
    )";
    
    setStyleSheet(style);
    m_stylesApplied = true;
}

void PatientPage::updateScaling() {
    if (width() < 100 || height() < 100) return;

    // Search bar height: 4-5% of height
    int searchH = percentHeight(5, 35, 55);
    int fontSize = qBound(14, percentHeight(2), 24);
    searchBar->setFixedHeight(searchH);
    searchBar->setStyleSheet(QString(
        "#searchBar { font-size: %1px; border: 1px solid #ccc; border-radius: 6px; padding: 8px; background: #fafafa; }"
        "#searchBar:focus { border: 2px solid #003366; background: white; }"
    ).arg(fontSize));

    // Add button
    int btnPaddingV = percentHeight(2, 5, 16);
    int btnPaddingH = percentWidth(1, 15, 30);
    int btnFontSize = qBound(14, percentHeight(1.8), 22);
    addButton->setStyleSheet(QString(
        "#addButton { font-size: %1px; padding: %2px %3px; background-color: #003366; color: white; border: none; border-radius: 6px; font-weight: bold; }"
        "#addButton:hover { background-color: #0059b3; }"
    ).arg(btnFontSize).arg(btnPaddingV).arg(btnPaddingH));

    // Table row height - only update row heights, not stylesheet
    int rowH = percentHeight(6, 45, 70);
    for (int i = 0; i < patientTable->rowCount(); ++i) {
        patientTable->setRowHeight(i, rowH);
    }
    
    // Update header height
    patientTable->horizontalHeader()->setFixedHeight(rowH);
}

void PatientPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);  // Call QWidget's showEvent, not ResponsiveWidget
    
    // Delay data loading slightly to avoid visual glitch
    QTimer::singleShot(50, this, [this]() {
        loadPatientsFromDatabase();
        updateScaling();
    });
}

void PatientPage::loadPatientsFromDatabase() {
    QSqlQuery query("SELECT patient_id, first_name, last_name, dob, gender FROM patients");

    if (!query.exec()) {
        qWarning() << "❌ SQL Error:" << query.lastError().text();
        return;
    }

    patientTable->setRowCount(0);

    // Calculate sizes based on current dimensions
    int rowH = percentHeight(6, 45, 70);
    int btnFontSize = qBound(12, percentHeight(2), 20);
    int btnPadding = qBound(8, percentHeight(1), 18);

    int row = 0;
    while (query.next()) {
        patientTable->insertRow(row);
        patientTable->setRowHeight(row, rowH);

        for (int col = 0; col < 5; ++col) {
            patientTable->setItem(row, col, new QTableWidgetItem(query.value(col).toString()));
        }

        // Action button
        QPushButton *arrowButton = new QPushButton("➔");
        arrowButton->setCursor(Qt::PointingHandCursor);
        arrowButton->setStyleSheet(QString(
            "QPushButton {"
            "   background-color: #003366;"
            "   color: white;"
            "   border-radius: 6px;"
            "   padding: %1px %2px;"
            "   font-weight: bold;"
            "   font-size: %3px;"
            "}"
            "QPushButton:hover {"
            "   background-color: #0059b3;"
            "}"
        ).arg(btnPadding).arg(btnPadding + 2).arg(btnFontSize));

        QString patientId = query.value(0).toString();

        connect(arrowButton, &QPushButton::clicked, this, [this, patientId]() {
            emit viewSurgeryDetailsRequested(patientId);
        });

        QWidget *actionWidget = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(actionWidget);
        layout->addWidget(arrowButton);
        layout->setAlignment(Qt::AlignCenter);
        layout->setContentsMargins(1, 1, 1, 1);
        actionWidget->setLayout(layout);

        patientTable->setCellWidget(row, 5, actionWidget);
        ++row;
    }
}

void PatientPage::onAddPatientClicked() {
    AddPatientDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        loadPatientsFromDatabase();
    }
}

void PatientPage::onRowDoubleClicked(int row, int column) {
    Q_UNUSED(column);
    if (row >= 0) {
        QString patientId = patientTable->item(row, 0)->text();
        emit viewSurgeryDetailsRequested(patientId);
    }
}

void PatientPage::onSearchTextChanged(const QString &text) {
    for (int row = 0; row < patientTable->rowCount(); ++row) {
        bool match = false;
        for (int col = 0; col < patientTable->columnCount() - 1; ++col) {
            QTableWidgetItem *item = patientTable->item(row, col);
            if (item && item->text().contains(text, Qt::CaseInsensitive)) {
                match = true;
                break;
            }
        }
        patientTable->setRowHidden(row, !match);
    }
}