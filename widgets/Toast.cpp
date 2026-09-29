#include "Toast.hpp"

#include <QLabel>
#include <QTimer>
#include <QWidget>

namespace Toast {

void show(QWidget* parent, const QString& message, int durationMs, const QString& background) {
    if (!parent)
        return;
    const int fontPx = qBound(16, parent->height() * 22 / 1000, 32);
    const int pad = fontPx * 2 / 3;

    auto* toast = new QLabel(message, parent);
    toast->setStyleSheet(QString("background-color: %1; color: white; padding: %2px %3px; "
                                 "border-radius: %4px; font-size: %5px;")
                             .arg(background).arg(pad).arg(pad * 2).arg(pad).arg(fontPx));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    toast->adjustSize();
    toast->move((parent->width() - toast->width()) / 2,
                parent->height() - toast->height() - parent->height() / 12);
    toast->show();
    toast->raise();

    QTimer::singleShot(durationMs, toast, &QLabel::deleteLater);
}

} // namespace Toast
