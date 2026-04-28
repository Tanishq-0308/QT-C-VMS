#pragma once
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

/**
 * UIScale - Utility class for responsive UI scaling
 * 
 * Usage:
 *   int size = UIScale::scaled(100);  // Scale 100px based on screen
 *   int w = UIScale::percentWidth(widget, 25);  // 25% of widget width
 *   int fontSize = UIScale::fontSize(18, widget);  // Scaled font size
 */


class UIScale {
public:

    static constexpr int REF_WIDTH = 3840;
    static constexpr int REF_HEIGHT = 2160;


    // Get scale factor based on primary screen
    static double factor() {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen) return 1.0;
        int width = screen->geometry().width();
        return static_cast<double>(width) / REF_WIDTH;
    }

    // Get scale factor based on widget's current screen
    static double factor(QWidget* widget) {
        if (!widget || !widget->window()) return factor();
        QScreen* screen = widget->window()->screen();
        if (!screen) return factor();
        return static_cast<double>(screen->geometry().width()) / REF_WIDTH;
    }

    // Scale a value with min/max bounds
    static int scaled(int value4k, int minVal, int maxVal, QWidget* widget = nullptr) {
        double f = widget ? factor(widget) : factor();
        return qBound(minVal, static_cast<int>(value4k * f), maxVal);
    }

    // Scale without bounds
    static int scaled(int value4k, QWidget* widget = nullptr) {
        double f = widget ? factor(widget) : factor();
        return static_cast<int>(value4k * f);
    }

    // Percentage of widget width
    static int percentWidth(QWidget* widget, int percent) {
        if (!widget) return 0;
        return widget->width() * percent / 100;
    }

    // Percentage of widget height
    static int percentHeight(QWidget* widget, int percent) {
        if (!widget) return 0;
        return widget->height() * percent / 100;
    }

    // Percentage with bounds
    static int percentWidth(QWidget* widget, int percent, int minVal, int maxVal) {
        return qBound(minVal, percentWidth(widget, percent), maxVal);
    }

    static int percentHeight(QWidget* widget, int percent, int minVal, int maxVal) {
        return qBound(minVal, percentHeight(widget, percent), maxVal);
    }

    // Font size that scales with bounds
    static int fontSize(int size4k, QWidget* widget = nullptr) {
        return qBound(8, scaled(size4k, widget), 72);
    }

    // Font size with custom bounds
    static int fontSize(int size4k, int minSize, int maxSize, QWidget* widget = nullptr) {
        return qBound(minSize, scaled(size4k, widget), maxSize);
    }

    // Spacing/margin scaling
    static int spacing(int space4k, QWidget* widget = nullptr) {
        return qBound(2, scaled(space4k, widget), 100);
    }

    // Border radius scaling
    static int radius(int radius4k, QWidget* widget = nullptr) {
        return qBound(2, scaled(radius4k, widget), 50);
    }
};