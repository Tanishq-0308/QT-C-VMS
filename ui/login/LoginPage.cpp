#include "LoginPage.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFile>
#include <QApplication>
#include <QDebug>

LoginPage::LoginPage(QWidget* parent) : ResponsiveWidget(parent) {
    setupUI();
}

void LoginPage::setupUI(){
    setObjectName("loginRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    // Outer layout - centers the container
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setAlignment(Qt::AlignCenter);

    // Container - will be sized dynamically in updateScaling()
    container = new QWidget(this);
    container->setObjectName("loginContainer");

    containerLayout = new QVBoxLayout(container);

    // Logo
    logo = new QLabel();
    logo->setAlignment(Qt::AlignCenter);
    logo->setObjectName("logo");

    // Username
    auto* userLabel = new QLabel("UserId *");
    userLabel->setObjectName("fieldLabel");

    usernameEdit = new QLineEdit("admin");
    usernameEdit->setPlaceholderText("Enter Your UserId");
    usernameEdit->setObjectName("usernameEdit");
    usernameEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Password
    auto* passLabel = new QLabel("Password *");
    passLabel->setObjectName("fieldLabel");

    passwordEdit = new QLineEdit("123456");
    passwordEdit->setPlaceholderText("Enter Your Password");
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setObjectName("passwordEdit");
    passwordEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    togglePasswordBtn = new QPushButton("Show");
    togglePasswordBtn->setObjectName("toggleBtn");
    togglePasswordBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(togglePasswordBtn, &QPushButton::clicked, this, &LoginPage::togglePasswordVisibility);

    auto* passLayout = new QHBoxLayout();
    passLayout->setSpacing(10);
    passLayout->addWidget(passwordEdit, 1); // stretch factor 1
    passLayout->addWidget(togglePasswordBtn, 0); // no stretch

    // Login Button
    loginBtn = new QPushButton("Login");
    loginBtn->setObjectName("loginButton");
    loginBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    loginBtn->setCursor(Qt::PointingHandCursor);
    connect(loginBtn, &QPushButton::clicked, this, &LoginPage::handleLogin);

    // Status Label
    statusLabel = new QLabel();
    statusLabel->setObjectName("statusLabel");
    statusLabel->setAlignment(Qt::AlignCenter);

    // Add widgets to layout
    containerLayout->addWidget(logo);
    containerLayout->addSpacing(10);
    containerLayout->addWidget(userLabel);
    containerLayout->addWidget(usernameEdit);
    containerLayout->addSpacing(5);
    containerLayout->addWidget(passLabel);
    containerLayout->addLayout(passLayout);
    containerLayout->addSpacing(10);
    containerLayout->addWidget(loginBtn);
    containerLayout->addWidget(statusLabel);

    outerLayout->addWidget(container);
}

void LoginPage::updateScaling(){
    // Skip if not visible or too small
    if (width() < 100 || height() < 100) return;

    // Container: 25% of width, 45% of height, with min/max bounds
    int containerW = percentWidth(25, 300, 600);
    int containerH = percentHeight(45, 400, 700);

        qDebug() << "Window size:" << width() << "x" << height();
    qDebug() << "Container size:" << containerW << "x" << containerH;

    container->setFixedSize(containerW, containerH);

    // Margins: 8% of container width
    int margin = containerW * 8 / 100;
    int spacing = containerH * 4 / 100;
    containerLayout->setContentsMargins(margin, margin, margin, margin);
    containerLayout->setSpacing(spacing);

    // Logo: 50% of container width, 18% of container height
    int logoW = containerW * 30 / 100;
    int logoH = containerH * 18 / 100;
    logo->setPixmap(QPixmap(":/assets/logo.png").scaled(
        logoW, logoH, Qt::KeepAspectRatio, Qt::SmoothTransformation
    ));

    // Input fields height: 10% of container height
    int inputH = qBound(30, containerH * 10 / 100, 50);
    usernameEdit->setFixedHeight(inputH);
    passwordEdit->setFixedHeight(inputH);
    togglePasswordBtn->setFixedHeight(inputH);
    loginBtn->setFixedHeight(inputH);

    // Apply dynamic stylesheet
    applyDynamicStyles();
}

void LoginPage::applyDynamicStyles() {
    int containerW = container->width();
    int containerH = container->height();

    // Calculate dynamic sizes
    int fontSize = qBound(11, containerH * 4 / 100, 18);
    int labelSize = qBound(10, containerH * 35 / 1000, 14);
    int padding = qBound(6, containerW * 3 / 100, 15);
    int radius = qBound(4, containerW * 2 / 100, 12);

    QString style = QString(R"(
        #loginRoot {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #b31217, stop:1 #2c3e50);
        }
        
        #loginContainer {
            background: white;
            border-radius: %1px;
            border: 1px solid #e0e0e0;
        }
        
        #fieldLabel {
            font-size: %2px;
            color: #555;
            font-weight: 500;
        }
        
        #inputField {
            font-size: %3px;
            padding: %4px;
            border: 1px solid #ddd;
            border-radius: %5px;
            background: #fafafa;
        }
        
        #inputField:focus {
            border: 2px solid #007bff;
            background: white;
        }
        
        #loginButton {
            background-color: #007bff;
            color: white;
            font-size: %3px;
            font-weight: bold;
            border: none;
            border-radius: %5px;
        }
        
        #loginButton:hover {
            background-color: #0056b3;
        }
        
        #loginButton:pressed {
            background-color: #004494;
        }
        
        #toggleBtn {
            font-size: %2px;
            padding: %6px %4px;
            background: #f0f0f0;
            border: 1px solid #ddd;
            border-radius: %5px;
        }
        
        #toggleBtn:hover {
            background: #e0e0e0;
        }
        
        #statusLabel {
            font-size: %2px;
            color: #dc3545;
        }
    )")
    .arg(radius)        // %1 - container border radius
    .arg(labelSize)     // %2 - label font size
    .arg(fontSize)      // %3 - input/button font size
    .arg(padding)       // %4 - padding
    .arg(radius - 2)    // %5 - input border radius
    .arg(padding / 2);  // %6 - toggle button padding

    setStyleSheet(style);
}

void LoginPage::togglePasswordVisibility() {
    bool isHidden = passwordEdit->echoMode() == QLineEdit::Password;
    passwordEdit->setEchoMode(isHidden ? QLineEdit::Normal : QLineEdit::Password);
    togglePasswordBtn->setText(isHidden ? "Hide" : "Show");
}

void LoginPage::handleLogin() {
    const QString user = usernameEdit->text();
    const QString pass = passwordEdit->text();

    if (user == "admin" && pass == "123456") {
        // statusLabel->setText("✅ Login successful");
        emit loginSuccessful();
    } else {
        statusLabel->setText("❌ Invalid username or password");
    }
}
