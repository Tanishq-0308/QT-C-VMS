
#pragma once
#include <QObject>
#include <QJsonObject>

class UsbMountController : public QObject {
    Q_OBJECT
public:
    explicit UsbMountController(QObject *parent = nullptr);

    QJsonObject mount();
    QJsonObject unmount();
};