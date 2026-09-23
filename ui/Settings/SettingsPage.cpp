#include "SettingsPage.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QScrollArea>
#include <QSpacerItem>
#include <QSizePolicy>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>
#include <QDebug>
#include <QCoreApplication>
#include <QComboBox>
#include <QTimer>
#include <QDir>

// ============== Responsive Helpers ==============

int SettingsPage::percentWidth(int percent, int minVal, int maxVal) {
    int val = width() * percent / 100;
    return qBound(minVal, val, maxVal);
}

int SettingsPage::percentHeight(int percent, int minVal, int maxVal) {
    int val = height() * percent / 100;
    return qBound(minVal, val, maxVal);
}

// ============== Event Handlers ==============

void SettingsPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateScaling();
}

void SettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(50, this, [this]() {
        updateScaling();
    });
}

// ============== Constructor ==============

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
    setupUI();
    applyStyles();
    loadSettingsFromDatabase();
}

void SettingsPage::setupUI() {
    mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);
    mainLayout->setContentsMargins(400, 350, 400, 0);
    
    this->setObjectName("settingsPage");
    this->setAttribute(Qt::WA_StyledBackground, true);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setAlignment(Qt::AlignCenter);
    
    scrollContent = new QWidget;
    scrollContent->setObjectName("scrollContent");
    scrollArea->setWidget(scrollContent);
    
    scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setAlignment(Qt::AlignTop);

    QVBoxLayout *outerWrapper = new QVBoxLayout;
    outerWrapper->setAlignment(Qt::AlignTop);

    QSpacerItem *bottomSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

    twoColLayout = new QHBoxLayout;
    leftCol = new QVBoxLayout;
    rightCol = new QVBoxLayout;

    // Helper lambda
    auto addField = [](QVBoxLayout *col, const QString &labelText, QWidget *inputWidget, QLabel** labelOut = nullptr) {
        QLabel *label = new QLabel(labelText);
        label->setStyleSheet("font-weight: bold; background-color: transparent; color:white;");
        QVBoxLayout *vbox = new QVBoxLayout;
        vbox->addWidget(label);
        vbox->addWidget(inputWidget);
        col->addLayout(vbox);
        col->addSpacing(50);
        if (labelOut) *labelOut = label;
    };

    // ============== Left Column ==============
    
    // Logo field
    logoPathEdit = new QLineEdit;
    logoPathEdit->setReadOnly(true);
    uploadLogoBtn = new QPushButton("Upload");
    
    QHBoxLayout *logoRow = new QHBoxLayout;
    logoRow->addWidget(logoPathEdit);
    logoRow->addWidget(uploadLogoBtn);
    QWidget *logoWidget = new QWidget;
    logoWidget->setObjectName("watermarkWidget");
    logoWidget->setLayout(logoRow);
    
    logoLabel = new QLabel("Hospital Logo (png) *");
    logoLabel->setStyleSheet("font-weight: bold; background-color: transparent; color:white;");
    QVBoxLayout *logoVbox = new QVBoxLayout;
    logoVbox->addWidget(logoLabel);
    logoVbox->addWidget(logoWidget);
    leftCol->addLayout(logoVbox);
    leftCol->addSpacing(50);

    hospitalNameEdit = new QLineEdit;
    addField(leftCol, "Hospital Name *", hospitalNameEdit, &hospitalNameLabel);

    softwareNameEdit = new QLineEdit;
    addField(leftCol, "Software Name *", softwareNameEdit, &softwareNameLabel);

    hospitalEmailEdit = new QLineEdit;
    addField(leftCol, "Hospital Email *", hospitalEmailEdit, &hospitalEmailLabel);

    hospitalAddressEdit = new QTextEdit;
    addField(leftCol, "Hospital Address *", hospitalAddressEdit, &hospitalAddressLabel);

    stateEdit = new QLineEdit;
    addField(leftCol, "State *", stateEdit, &stateLabel);

    // ============== Right Column ==============

    districtEdit = new QLineEdit;
    addField(rightCol, "District *", districtEdit, &districtLabel);

    pinEdit = new QLineEdit;
    addField(rightCol, "Pin *", pinEdit, &pinLabel);

    aboutHospitalEdit = new QTextEdit;
    addField(rightCol, "About Hospital *", aboutHospitalEdit, &aboutHospitalLabel);

    hospitalPhoneEdit = new QLineEdit;
    addField(rightCol, "Hospital Phone *", hospitalPhoneEdit, &hospitalPhoneLabel);

    videoInputCombo = new QComboBox;
    videoInputCombo->addItem("HDMI");
    videoInputCombo->addItem("SDI");
    videoInputCombo->addItem("AHD");
    addField(rightCol, "Video Input", videoInputCombo, &videoInputLabel);

    // ─── Show/Hide Video Label Toggle ───────────
    showLabelCheckbox = new QCheckBox("Enable");
    showLabelCheckbox->setChecked(true);
    showLabelLabel = new QLabel("Video Overlay");
    showLabelLabel->setStyleSheet("font-weight: bold; background-color: transparent; color:white;");
    
    // Create a container widget like other fields
    QWidget* checkboxContainer = new QWidget;
    checkboxContainer->setStyleSheet("background-color: white; border-radius: 6px; padding: 8px;");
    QHBoxLayout* checkboxLayout = new QHBoxLayout(checkboxContainer);
    checkboxLayout->setContentsMargins(12, 8, 12, 8);
    showLabelCheckbox->setStyleSheet(
        "QCheckBox { color: black; font-size: 16px; background: transparent; }"
        "QCheckBox::indicator { width: 22px; height: 22px; }"
        "QCheckBox::indicator:unchecked { border: 2px solid #666; border-radius: 4px; background: white; }"
        "QCheckBox::indicator:checked { border: 2px solid #003366; border-radius: 4px; background: #003366; }"
    );
    checkboxLayout->addWidget(showLabelCheckbox);
    checkboxLayout->addStretch();
    
    QVBoxLayout *labelToggleVbox = new QVBoxLayout;
    labelToggleVbox->addWidget(showLabelLabel);
    labelToggleVbox->addWidget(checkboxContainer);
    rightCol->addLayout(labelToggleVbox);
    rightCol->addSpacing(50);

    // Hidden fields (not in any layout; parented to this so they are deleted
    // with the page, and hidden so they are not shown at (0,0) as orphan children)
    watermarkPathEdit = new QLineEdit(this);
    watermarkPathEdit->setReadOnly(true);
    watermarkPathEdit->hide();
    uploadWatermarkBtn = new QPushButton("Upload", this);
    uploadWatermarkBtn->hide();
    storagePathEdit = new QLineEdit(this);
    storagePathEdit->hide();
    rtspLinkEdit = new QLineEdit(this);
    rtspLinkEdit->setEchoMode(QLineEdit::Password);
    rtspLinkEdit->hide();
    togglePasswordBtn = new QPushButton("Show", this);
    togglePasswordBtn->hide();

    twoColLayout->addLayout(leftCol);
    twoColLayout->addSpacing(90);
    twoColLayout->addLayout(rightCol);

    outerWrapper->addLayout(twoColLayout);

    saveButton = new QPushButton("Save");
    saveButton->setFixedWidth(150);
    saveButton->setObjectName("save");
    outerWrapper->addWidget(saveButton, 0, Qt::AlignHCenter);

    outerWrapper->addItem(bottomSpacer);
    scrollLayout->addLayout(outerWrapper);
    mainLayout->addWidget(scrollArea);

    // ============== Connections ==============
    
    connect(uploadLogoBtn, &QPushButton::clicked, this, [=]() {
        QString usbMountPath = "/media/brainwave";
        QDir usbRoot(usbMountPath);
        QStringList drives = usbRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        if (drives.isEmpty()) {
            QMessageBox::warning(this, "USB Not Found", "No USB drive detected. Please insert the pendrive.");
            return;
        }

        QString usbDrive = usbMountPath + "/" + drives.first();
        QString logoInUsb = usbDrive + "/logo.png";

        if (!QFile::exists(logoInUsb)) {
            QMessageBox::warning(this, "Logo Not Found", "No 'logo.png' found in the USB drive.");
            return;
        }

        logoPathEdit->setText(logoInUsb);
        QString destLogoPath = QCoreApplication::applicationDirPath() + "/logo.png";

        if (QFile::exists(destLogoPath)) {
            QFile::remove(destLogoPath);
        }

        if (QFile::copy(logoInUsb, destLogoPath)) {
            emit logoChanged(destLogoPath);
        } else {
            QMessageBox::warning(this, "Error", "Failed to copy logo from USB.");
        }
    });

    connect(togglePasswordBtn, &QPushButton::clicked, this, &SettingsPage::togglePasswordVisibility);
    connect(saveButton, &QPushButton::clicked, this, &SettingsPage::saveSettingsToDatabase);
}

void SettingsPage::applyStyles() {
    this->setStyleSheet(R"(
        #settingsPage {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                        stop:0 #C02425, stop:1 #302b63);
            color: white;
            font-family: "Segoe UI";
        }
        #scrollContent {
            background: transparent;
            color: white;
            font-family: "Segoe UI";
        }
        QScrollArea {
            background: transparent;
            border: none;
        }
        #watermarkWidget {
            background-color: transparent;
        }
    )");
}

void SettingsPage::updateScaling() {
    if (width() < 100 || height() < 100) return;
    
    int widthDiff = qAbs(width() - m_lastWidth);
    int heightDiff = qAbs(height() - m_lastHeight);
    if (widthDiff < 20 && heightDiff < 20 && m_lastWidth > 0) {
        return;
    }
    m_lastWidth = width();
    m_lastHeight = height();

    // ============== Margins ==============
    int marginH = percentWidth(10, 50, 400);
    int marginV = percentHeight(15, 50, 350);
    mainLayout->setContentsMargins(marginH, marginV, marginH, 0);

    // ============== Font Sizes ==============
    int labelFontSize = percentHeight(2, 14, 22);
    int inputFontSize = percentHeight(2.2, 16, 26);
    int buttonFontSize = percentHeight(1.8, 14, 20);
    int checkboxFontSize = percentHeight(1.8, 14, 20);

    // ============== Input Heights ==============
    int lineEditHeight = percentHeight(4, 30, 45);
    int checkboxSize = percentHeight(2.5, 18, 28);

    // ============== Label Style ==============
    QString labelStyle = QString(
        "font-weight: bold; background-color: transparent; color: white; font-size: %1px;"
    ).arg(labelFontSize);

    QLabel* labels[] = {
        logoLabel, hospitalNameLabel, softwareNameLabel, hospitalEmailLabel,
        hospitalAddressLabel, stateLabel, districtLabel, pinLabel,
        aboutHospitalLabel, hospitalPhoneLabel, videoInputLabel, showLabelLabel
    };
    for (QLabel* lbl : labels) {
        if (lbl) lbl->setStyleSheet(labelStyle);
    }

    // ============== Line Edit Style ==============
    QString lineEditStyle = QString(
        "QLineEdit { height: %1px; font-size: %2px; }"
    ).arg(lineEditHeight).arg(inputFontSize);

    QLineEdit* edits[] = {
        logoPathEdit, hospitalNameEdit, softwareNameEdit, hospitalEmailEdit,
        stateEdit, districtEdit, pinEdit, hospitalPhoneEdit
    };
    for (QLineEdit* edit : edits) {
        if (edit) edit->setStyleSheet(lineEditStyle);
    }

    // ============== Text Edit Style ==============
    QString textEditStyle = QString("QTextEdit { font-size: %1px; }").arg(inputFontSize);
    hospitalAddressEdit->setStyleSheet(textEditStyle);
    aboutHospitalEdit->setStyleSheet(textEditStyle);

    // ============== Combo Box Style ==============
    QString comboStyle = QString(
        "QComboBox { background-color: white; color: black; padding: 6px 12px; "
        "border-radius: 6px; font-size: %1px; }"
        "QComboBox QAbstractItemView { background-color: white; color: black; font-size: %1px; }"
    ).arg(inputFontSize);
    videoInputCombo->setStyleSheet(comboStyle);

    // ============== Checkbox Style ==============
    QString checkboxStyle = QString(
        "QCheckBox { color: black; font-size: %1px; background: transparent; }"
        "QCheckBox::indicator { width: %2px; height: %2px; }"
        "QCheckBox::indicator:unchecked { border: 2px solid #666; border-radius: 4px; background: white; }"
        "QCheckBox::indicator:checked { border: 2px solid #003366; border-radius: 4px; background: #003366; }"
    ).arg(checkboxFontSize).arg(checkboxSize);
    showLabelCheckbox->setStyleSheet(checkboxStyle);

    // ============== Button Styles ==============
    int btnPadding = percentHeight(1.2, 10, 18);
    QString btnStyle = QString(
        "QPushButton { font-size: %1px; padding: %2px %2px; }"
    ).arg(buttonFontSize).arg(btnPadding);
    
    uploadLogoBtn->setStyleSheet(btnStyle);

    // Save button
    int saveFontSize = percentHeight(2.2, 18, 26);
    int saveWidth = percentWidth(8, 100, 180);
    saveButton->setStyleSheet(QString("QPushButton { font-size: %1px; padding: 15px 15px; }").arg(saveFontSize));
    saveButton->setFixedWidth(saveWidth);
}

void SettingsPage::togglePasswordVisibility() {
    bool isHidden = rtspLinkEdit->echoMode() == QLineEdit::Password;
    rtspLinkEdit->setEchoMode(isHidden ? QLineEdit::Normal : QLineEdit::Password);
    togglePasswordBtn->setText(isHidden ? "Hide" : "Show");
}

void SettingsPage::loadSettingsFromDatabase() {
    QSqlQuery query;
    if (query.exec("SELECT * FROM settings ORDER BY id LIMIT 1")) {
        if (query.next()) {
            logoPathEdit->setText(query.value("hospital_logo_path").toString());
            hospitalNameEdit->setText(query.value("hospital_name").toString());
            softwareNameEdit->setText(query.value("software_name").toString());
            hospitalEmailEdit->setText(query.value("hospital_email").toString());
            hospitalAddressEdit->setText(query.value("hospital_address").toString());
            stateEdit->setText(query.value("state").toString());
            districtEdit->setText(query.value("district").toString());
            pinEdit->setText(query.value("pin").toString());
            aboutHospitalEdit->setText(query.value("about_hospital").toString());
            watermarkPathEdit->setText(query.value("watermark_logo_path").toString());
            storagePathEdit->setText(query.value("storage_path").toString());
            hospitalPhoneEdit->setText(query.value("hospital_phone").toString());
            rtspLinkEdit->setText(query.value("rtsp_link").toString());
            videoInputCombo->setCurrentText(query.value("video_input").toString());
            
            // Load show_video_label (default to true if column doesn't exist)
            QVariant showLabelVal = query.value("show_video_label");
            bool showLabel = showLabelVal.isNull() ? true : (showLabelVal.toInt() == 1);
            showLabelCheckbox->setChecked(showLabel);
        }
    } else {
        qDebug() << "Load failed:" << query.lastError().text();
    }
}

void SettingsPage::saveSettingsToDatabase() {
    QSqlQuery checkQuery("SELECT COUNT(*) FROM settings");
    bool exists = checkQuery.next() && checkQuery.value(0).toInt() > 0;

    QSqlQuery query;
    if (exists) {
        query.prepare("UPDATE settings SET hospital_logo_path=?, hospital_name=?, software_name=?, "
                      "hospital_email=?, hospital_address=?, state=?, district=?, pin=?, "
                      "about_hospital=?, watermark_logo_path=?, storage_path=?, hospital_phone=?, "
                      "rtsp_link=?, video_input=?, show_video_label=? "
                      "WHERE id = (SELECT MIN(id) FROM settings)");
    } else {
        query.prepare("INSERT INTO settings (hospital_logo_path, hospital_name, software_name, "
                      "hospital_email, hospital_address, state, district, pin, about_hospital, "
                      "watermark_logo_path, storage_path, hospital_phone, rtsp_link, video_input, "
                      "show_video_label) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    }

    query.addBindValue(logoPathEdit->text());
    query.addBindValue(hospitalNameEdit->text());
    query.addBindValue(softwareNameEdit->text());
    query.addBindValue(hospitalEmailEdit->text());
    query.addBindValue(hospitalAddressEdit->toPlainText());
    query.addBindValue(stateEdit->text());
    query.addBindValue(districtEdit->text());
    query.addBindValue(pinEdit->text());
    query.addBindValue(aboutHospitalEdit->toPlainText());
    query.addBindValue(watermarkPathEdit->text());
    query.addBindValue(storagePathEdit->text());
    query.addBindValue(hospitalPhoneEdit->text());
    query.addBindValue(rtspLinkEdit->text());
    query.addBindValue(videoInputCombo->currentText());
    query.addBindValue(showLabelCheckbox->isChecked() ? 1 : 0);

    if (!query.exec()) {
        qDebug() << "Save failed:" << query.lastError().text();
        QMessageBox::warning(this, "Error", "Failed to save settings.");
    } else {
        QMessageBox::information(this, "Success", "Settings saved successfully.");
        emit videoInputChanged(videoInputCombo->currentText());
        emit videoLabelVisibilityChanged(showLabelCheckbox->isChecked());
    }
}