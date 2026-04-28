#include "ProfilePage.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpacerItem>

// ProfilePage.cpp

ProfilePage::ProfilePage(QWidget *parent) : QWidget(parent) {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 1. Centered "User Profile" Heading
    QLabel *titleLabel = new QLabel("User Profile");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 44px; font-weight: bold; color: red;");
    mainLayout->addWidget(titleLabel);

    this->setStyleSheet(R"(
            QLineEdit {

            }
        )");

    // Spacer
    mainLayout->addSpacing(100);

    // 2. Main horizontal layout (avatar on left, form on right)
    QHBoxLayout *contentLayout = new QHBoxLayout();

    // 3. Avatar Circle
    QLabel *avatarLabel = new QLabel("B");
    avatarLabel->setFixedSize(330, 330);
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setStyleSheet("background-color: hotpink; border-radius: 65px; font-size: 46px; font-weight: bold;");
    contentLayout->addWidget(avatarLabel);
    contentLayout->addSpacing(60); // spacing between avatar and form

    // 4. Form layout (on right)
    QVBoxLayout *formLayout = new QVBoxLayout();
    formLayout->setAlignment(Qt::AlignTop);

    // Helper to add field
    auto addField = [&](const QString &labelText) {
        QLabel *label = new QLabel(labelText);
        label->setStyleSheet("font-weight: 600; font-size: 25px;");
        QLineEdit *edit = new QLineEdit();
        edit->setFixedWidth(600); // form width
        edit->setFixedHeight(50);  // Increase field height
        edit->setStyleSheet(R"(
        QLineEdit {
            font-size: 20px;
            padding: 8px;
            border: 1px solid gray;
            border-radius: 5px;
        }
    )");
        formLayout->addWidget(label);
        formLayout->addWidget(edit);
        formLayout->addSpacing(14);
    };

    addField("Name");
    addField("Email");

    // Update button
    QPushButton *updateButton = new QPushButton("UPDATE");
    updateButton->setFixedWidth(600);
    updateButton->setStyleSheet("background-color: #003366; color: white; font-size:20px; font-weight: bold; border-radius: 6px; padding: 8px;");
    formLayout->addWidget(updateButton);

    contentLayout->addLayout(formLayout);

    // Add to main layout
    mainLayout->addLayout(contentLayout);
    mainLayout->addStretch();
}

