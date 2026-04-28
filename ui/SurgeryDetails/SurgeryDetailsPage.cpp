#include "SurgeryDetailsPage.hpp"
#include "../AddSurgeryDialog/AddSurgeryDialog.hpp"
#include "../EditPatientDialog/EditPatientDialog.hpp"
#include <QHeaderView>
#include <QFile>
#include <QApplication>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDate>
#include <QTimer>


SurgeryDetailsPage::SurgeryDetailsPage(QWidget* parent) : ResponsiveWidget(parent) {
    setupUI();
    applyStyles();
}

void SurgeryDetailsPage::setupUI() {
    auto mainLayout = new QVBoxLayout(this);

    // Top Back Button
    auto topLayout = new QHBoxLayout();
    backButton = new QPushButton("Go Back");
    backButton->setObjectName("backButton");
    backButton->setCursor(Qt::PointingHandCursor);
    topLayout->addWidget(backButton);
    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    // Patient Details Header
    patientTitle = new QLabel("Patient Details");
    patientTitle->setAlignment(Qt::AlignCenter);
    patientTitle->setObjectName("patientTitle");
    mainLayout->addWidget(patientTitle);

    // Patient Details Grid
    patientGrid = new QGridLayout();
    QStringList labels = {"First Name", "Last Name", "Date Of Birth", "Age",
                          "Gender", "Mobile", "Address", "Patient Id"};

    for (int i = 0; i < labels.size(); ++i) {
        QLabel* keyLabel = createLabel(labels[i] + " :", false);
        QLabel* valueLabel = createLabel("", false);
        keyLabel->setObjectName("keyLabel");
        valueLabel->setObjectName("valueLabel");
        
        keyLabels[labels[i]] = keyLabel;
        valueLabels[labels[i]] = valueLabel;

        int row = i / 2;
        int col = (i % 2) * 2;

        patientGrid->addWidget(keyLabel, row, col);
        patientGrid->addWidget(valueLabel, row, col + 1);
        patientGrid->setAlignment(Qt::AlignCenter);
    }

    mainLayout->addLayout(patientGrid);

    // Edit Patient Button
    editPatientButton = new QPushButton("Edit Patient");
    editPatientButton->setObjectName("editButton");
    editPatientButton->setCursor(Qt::PointingHandCursor);
    mainLayout->addWidget(editPatientButton, 0, Qt::AlignCenter);

    connect(editPatientButton, &QPushButton::clicked, this, [=]() {
        EditPatientDialog *dialog = new EditPatientDialog(currentPatientId, this);
        if (dialog->exec() == QDialog::Accepted) {
            loadPatientData(currentPatientId);
        }
    });

    // Surgery List Header
    surgeryTitle = new QLabel("Surgery List");
    surgeryTitle->setAlignment(Qt::AlignCenter);
    surgeryTitle->setObjectName("surgeryTitle");
    mainLayout->addWidget(surgeryTitle);

    // Table and Add Button Layout
    auto tableHeaderLayout = new QHBoxLayout();
    tableHeaderLayout->addStretch();
    newSurgeryButton = new QPushButton("+ New Surgery");
    newSurgeryButton->setObjectName("newSurgeryButton");
    newSurgeryButton->setCursor(Qt::PointingHandCursor);
    tableHeaderLayout->addWidget(newSurgeryButton);
    mainLayout->addLayout(tableHeaderLayout);

    // Surgery Table
    surgeryTable = new QTableWidget(0, 6);
    surgeryTable->setObjectName("surgeryTable");
    QStringList headers = {"ID", "SURGEON NAME", "ADDITIONAL SURGEON", "ANESTHESIOLOGIST", "BODY PART", "ACTION"};
    surgeryTable->setHorizontalHeaderLabels(headers);
    surgeryTable->horizontalHeader()->setStretchLastSection(true);
    surgeryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    surgeryTable->verticalHeader()->setVisible(false);
    mainLayout->addWidget(surgeryTable);

    // Connections
    connect(backButton, &QPushButton::clicked, this, &SurgeryDetailsPage::goBackRequested);
    connect(editPatientButton, &QPushButton::clicked, this, &SurgeryDetailsPage::editPatientRequested);
    connect(newSurgeryButton, &QPushButton::clicked, [this]() {
        AddSurgeryDialog dialog(currentPatientId, this);
        connect(&dialog, &AddSurgeryDialog::surgeryAdded, [this]() {
            loadSurgeriesForPatient(currentPatientId);
        });
        dialog.exec();
    });
}

void SurgeryDetailsPage::applyStyles() {
    QString style = R"(
        #backButton {
            font-size: 18px;
            padding: 8px 16px;
            background-color: #002D62;
            border: 1px solid #ccc;
            border-radius: 6px;
        }
        
        #backButton:hover {
            background-color: #0059b3;
        }
        
        #patientTitle, #surgeryTitle {
            font-size: 24px;
            color: #c40000;
            font-weight: bold;
            padding: 10px;
        }
        
        #keyLabel {
            font-size: 16px;
        }
        
        #valueLabel {
            color: grey;
            font-size: 16px;
        }
        
        #editButton, #newSurgeryButton {
            background-color: #002D62;
            color: white;
            font-size: 18px;
            padding: 15px 20px;
            border: none;
            border-radius: 6px;
            font-weight: bold;
        }
        
        #editButton:hover, #newSurgeryButton:hover {
            background-color: #0059b3;
        }
        
        #editButton {
            margin: 20px;
        }
        
        #surgeryTable {
            font-size: 17px;
            border: 1px solid #ddd;
            gridline-color: #ddd;
            background-color: white;
        }
        
        #surgeryTable QHeaderView::section {
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

void SurgeryDetailsPage::updateScaling() {
    if (width() < 100 || height() < 100) return;

    // Title font sizes
    int titleSize = qBound(18, percentHeight(2.8), 30);
    patientTitle->setStyleSheet(QString("#patientTitle { font-size: %1px; color: #c40000; font-weight: bold; padding: 10px; }").arg(titleSize));
    surgeryTitle->setStyleSheet(QString("#surgeryTitle { font-size: %1px; color: #c40000; font-weight: bold; padding: 10px; }").arg(titleSize));

    // Label font sizes
    int labelSize = qBound(12, percentHeight(2), 20);
    for (auto it = keyLabels.begin(); it != keyLabels.end(); ++it) {
        it.value()->setStyleSheet(QString("#keyLabel {  font-size: %1px; }").arg(labelSize));
    }
    for (auto it = valueLabels.begin(); it != valueLabels.end(); ++it) {
        it.value()->setStyleSheet(QString("#valueLabel { color: grey; font-size: %1px; }").arg(labelSize));
    }

    // Button sizes
    int btnFontSize = qBound(14, percentHeight(1.8), 20);
    int btnPaddingV = percentHeight(2, 10, 20);
    int btnPaddingH = percentWidth(1, 15, 20);
    
    backButton->setStyleSheet(QString(
        "#backButton { font-size: %1px; padding: %2px %3px; background-color: #002D62; border: 1px solid #ccc; border-radius: 6px; }"
        "#backButton:hover { background-color: #0059b3; }"
    ).arg(btnFontSize).arg(btnPaddingV / 2).arg(btnPaddingH / 2));
    
    editPatientButton->setStyleSheet(QString(
        "#editButton { font-size: %1px; padding: %2px %3px; background-color: #002D62; color: white; border: none; border-radius: 6px; font-weight: bold; margin: 20px; }"
        "#editButton:hover { background-color: #0059b3; }"
    ).arg(btnFontSize).arg(btnPaddingV).arg(btnPaddingH));
    
    newSurgeryButton->setStyleSheet(QString(
        "#newSurgeryButton { font-size: %1px; padding: %2px %3px; background-color: #002D62; color: white; border: none; border-radius: 6px; font-weight: bold; }"
        "#newSurgeryButton:hover { background-color: #0059b3; }"
    ).arg(btnFontSize).arg(btnPaddingV).arg(btnPaddingH));

    // Table row height
    int rowH = percentHeight(7, 45, 70);
    for (int i = 0; i < surgeryTable->rowCount(); ++i) {
        surgeryTable->setRowHeight(i, rowH);
    }
    surgeryTable->horizontalHeader()->setFixedHeight(rowH);
}

QLabel* SurgeryDetailsPage::createLabel(const QString& text, bool bold) {
    auto label = new QLabel(text);
    QFont font;
    font.setBold(bold);
    label->setFont(font);
    return label;
}

void SurgeryDetailsPage::loadPatientData(const QString& patientId) {
    QSqlQuery query;
    query.prepare("SELECT first_name, last_name, dob, gender, mobile, address, patient_id "
                  "FROM patients WHERE patient_id = :id");

    query.bindValue(":id", patientId);

    qDebug() << "🔍 Fetching data for patientId:" << patientId;

    if (!query.exec()) {
        qWarning() << "❌ Failed to fetch patient:" << query.lastError().text();
        return;
    }

    if (!query.next()) {
        qWarning() << "⚠️ No patient found with ID:" << patientId;
        return;
    }
    
    QString dobStr = query.value(2).toString();
    QDate dob = QDate::fromString(dobStr, "yyyy-MM-dd");
    int age = dob.isValid() ? dob.daysTo(QDate::currentDate()) / 365 : 0;
    
    valueLabels["First Name"]->setText(query.value(0).toString());
    valueLabels["Last Name"]->setText(query.value(1).toString());
    valueLabels["Date Of Birth"]->setText(dobStr);
    valueLabels["Age"]->setText(QString::number(age));
    valueLabels["Gender"]->setText(query.value(3).toString());
    valueLabels["Mobile"]->setText(query.value(4).toString());
    valueLabels["Address"]->setText(query.value(5).toString());
    valueLabels["Patient Id"]->setText(query.value(6).toString());

    this->currentPatientId = patientId;
    loadSurgeriesForPatient(query.value(6).toString());
}

void SurgeryDetailsPage::loadSurgeriesForPatient(const QString& patientId) {
    qDebug() << "🔍 Loading surgeries for patient ID:" << patientId;

    surgeryTable->setRowCount(0);

    QSqlQuery query;
    query.prepare(R"(
        SELECT id, surgeon_name, additional_surgeon, anesthesiologist, body_part
        FROM surgeries
        WHERE patient_id = :id
    )");
    query.bindValue(":id", patientId);

    if (!query.exec()) {
        qWarning() << "❌ Failed to fetch surgeries:" << query.lastError().text();
        return;
    }

    // Calculate button sizes
    int btnFontSize = qBound(12, percentHeight(2), 20);
    int btnPadding = qBound(8, percentHeight(1), 18);
    int rowH = percentHeight(6, 45, 70);

    int row = 0;
    while (query.next()) {
        surgeryTable->insertRow(row);
        surgeryTable->setRowHeight(row, rowH);

        int surgeryId = query.value(0).toInt();

        for (int col = 0; col < 5; ++col) {
            QString value = query.value(col).toString();
            QTableWidgetItem* item = new QTableWidgetItem(value);
            surgeryTable->setItem(row, col, item);
        }

        // Action button
        QPushButton* actionBtn = new QPushButton("➔");
        actionBtn->setCursor(Qt::PointingHandCursor);
        actionBtn->setStyleSheet(QString(
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

        actionBtn->setProperty("surgeryId", surgeryId);
        
        connect(actionBtn, &QPushButton::clicked, this, [this, surgeryId]() {
            emit openSurgeryRecordingPage(currentPatientId, surgeryId);
        });

        QWidget *actionWidget = new QWidget;
        QHBoxLayout *layout = new QHBoxLayout(actionWidget);
        layout->addWidget(actionBtn);
        layout->setAlignment(Qt::AlignCenter);
        layout->setContentsMargins(1, 1, 1, 1);
        actionWidget->setLayout(layout);
    
        surgeryTable->setCellWidget(row, 5, actionWidget);
        ++row;
    }

    qDebug() << "✅ Total surgeries loaded:" << row;
}

void SurgeryDetailsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    
    QTimer::singleShot(50, this, [this]() {
        loadSurgeriesForPatient(currentPatientId);
        updateScaling();
    });
}