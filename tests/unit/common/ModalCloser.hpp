// Closes any application-modal widget (QDialog::exec(), QMessageBox::*,
// QInputDialog::getText) shortly after it appears, so tests can drive code
// paths that block in exec().  Records the class names of what it closed.
#pragma once

#include <QApplication>
#include <QDialog>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>
#include <QWidget>

namespace ts {

class ModalCloser : public QObject
{
public:
    explicit ModalCloser(int intervalMs = 5, QObject* parent = nullptr) : QObject(parent)
    {
        m_timer.setInterval(intervalMs);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_timer.start();
    }
    ~ModalCloser() override { m_timer.stop(); }

    QStringList closed;              // class names (+ text for message boxes)

private:
    void tick()
    {
        QWidget* w = QApplication::activeModalWidget();
        if (!w) return;
        QString name = QString::fromLatin1(w->metaObject()->className());
        if (auto* mb = qobject_cast<QMessageBox*>(w))
            name += ": " + mb->text();
        closed << name;
        if (auto* d = qobject_cast<QDialog*>(w))
            d->reject();
        else
            w->close();
    }
    QTimer m_timer;
};

} // namespace ts
