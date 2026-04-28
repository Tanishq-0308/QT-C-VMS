#include "ClickableLabel.hpp"
#include <QMouseEvent>

ClickableLabel::ClickableLabel(QWidget* parent)
    : QLabel(parent) {}

void ClickableLabel::setFilePath(const QString& path) {
    m_filePath = path;
}

QString ClickableLabel::filePath() const {
    return m_filePath;
}

void ClickableLabel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_filePath);
    }
}
