#pragma once
#include <QSlider>
#include <QMouseEvent>

class ClickableSlider : public QSlider {
    Q_OBJECT
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (orientation() == Qt::Horizontal) {
            int x = event->pos().x();
            double ratio = static_cast<double>(x) / width();
            int value = minimum() + ratio * (maximum() - minimum());
            setValue(value);
            emit sliderMoved(value);  // This moves the media position
        }
        QSlider::mousePressEvent(event);
    }
};
