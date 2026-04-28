#ifndef ADDCOMMENTDIALOG_H
#define ADDCOMMENTDIALOG_H

#include <QDialog>
#include <QTextEdit>

class AddCommentDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddCommentDialog(QWidget* parent = nullptr);
    QString getCommentText() const;

private:
    QTextEdit* commentEdit;
};

#endif // ADDCOMMENTDIALOG_H
