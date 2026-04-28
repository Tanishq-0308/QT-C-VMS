#pragma once
#include "core/ResponsiveWidget.hpp"
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

class LoginPage : public ResponsiveWidget {
    Q_OBJECT

public:
    explicit LoginPage(QWidget* parent = nullptr);

signals:
    void loginSuccessful();

protected:
    void updateScaling() override;

private slots:
    void togglePasswordVisibility();
    void handleLogin();

private:
    void setupUI();
    void applyDynamicStyles();

    // Widgets that need scaling access
    QWidget* container;
    QLabel* logo;
    QVBoxLayout* containerLayout;

    QLineEdit* usernameEdit;
    QLineEdit* passwordEdit;
    QPushButton* togglePasswordBtn;
    QPushButton* loginBtn;
    QLabel* statusLabel;
};
