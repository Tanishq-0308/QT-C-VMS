#include "AddCommentDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

AddCommentDialog::AddCommentDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Add Comment");

    QVBoxLayout* layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel("Enter your comment:"));

    commentEdit = new QTextEdit(this);
    layout->addWidget(commentEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton("OK", this);
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, this, &AddCommentDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &AddCommentDialog::reject);
}

QString AddCommentDialog::getCommentText() const {
    return commentEdit->toPlainText().trimmed();
}
