#ifndef PROFILEPAGE_HPP
#define PROFILEPAGE_HPP

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;

class ProfilePage : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilePage(QWidget *parent = nullptr);

private:
    QLabel *avatarLabel;
    QLineEdit *nameEdit;
    QLineEdit *emailEdit;
    QLineEdit *userIdEdit;
    QLineEdit *phoneEdit;
    QPushButton *updateButton;
};

#endif // PROFILEPAGE_HPP
