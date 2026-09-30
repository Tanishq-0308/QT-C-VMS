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
    toast->setObjectName("AppToast");
    toast->setStyleSheet(QString("background-color: %1; color: white; padding: %2px %3px; "
                                 "border-radius: %4px; font-size: %5px;")
                             .arg(background).arg(pad).arg(pad * 2).arg(pad).arg(fontPx));
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    toast->adjustSize();
    // Above any message still showing (e.g. two USB devices finishing together)
    int y = parent->height() - toast->height() - parent->height() / 12;
    for (QLabel* other : parent->findChildren<QLabel*>("AppToast", Qt::FindDirectChildrenOnly)) {
        if (other != toast && other->isVisible())
            y = qMin(y, other->y() - toast->height() - pad / 2);
    }
    toast->move((parent->width() - toast->width()) / 2, qMax(0, y));
    toast->show();
    toast->raise();

    QTimer::singleShot(durationMs, toast, &QLabel::deleteLater);
}

} // namespace Toast
