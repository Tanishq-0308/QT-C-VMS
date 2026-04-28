#pragma once
#include <QWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWindow>
#include <QScreen>
#include "core/UIScale.hpp"

/**
 * ResponsiveWidget - Base class for all responsive UI pages
 * 
 * Handles automatic UI scaling for:
 * - Window resize
 * - Monitor hot-swap (HDMI unplug/replug)
 * - Moving window between monitors
 * - Screen resolution changes
 * 
 * Usage:
 *   class MyPage : public ResponsiveWidget {
 *       Q_OBJECT
 *   public:
 *       MyPage(QWidget* parent = nullptr);
 *   protected:
 *       void updateScaling() override {
 *           // Update sizes based on percentWidth(), percentHeight()
 *       }
 *   };
 */
class ResponsiveWidget : public QWidget {
    Q_OBJECT

public:
    explicit ResponsiveWidget(QWidget* parent = nullptr) : QWidget(parent) {
        // Debounce timer to avoid too many updates during resize drag
        m_resizeTimer = new QTimer(this);
        m_resizeTimer->setSingleShot(true);
        m_resizeTimer->setInterval(100); // 100ms debounce - slower to prevent flickering
        connect(m_resizeTimer, &QTimer::timeout, this, &ResponsiveWidget::onResizeTimeout);
    }

    virtual ~ResponsiveWidget() = default;

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        m_resizeTimer->start();
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        
        // Connect to screen change signals (for hot-swap)
        connectScreenSignals();
        
        // Delay initial scaling to ensure widget is fully laid out
        QTimer::singleShot(0, this, &ResponsiveWidget::updateScaling);
    }

    /**
     * Override this method to update widget sizes, fonts, margins, etc.
     * Called automatically on resize, show, and screen change events.
     */
    virtual void updateScaling() = 0;

    // ---- Helper methods for derived classes ----

    // Get percentage of this widget's dimensions
    int percentWidth(int percent) const {
        return width() * percent / 100;
    }

    int percentHeight(int percent) const {
        return height() * percent / 100;
    }

    // Get percentage with min/max bounds
    int percentWidth(int percent, int minVal, int maxVal) const {
        return qBound(minVal, percentWidth(percent), maxVal);
    }

    int percentHeight(int percent, int minVal, int maxVal) const {
        return qBound(minVal, percentHeight(percent), maxVal);
    }

    // Get scale factor relative to 4K reference
    double scaleFactor() const {
        return UIScale::factor(const_cast<ResponsiveWidget*>(this));
    }

    // Scale a 4K value to current screen
    int scaled(int value4k) const {
        return UIScale::scaled(value4k, const_cast<ResponsiveWidget*>(this));
    }

    int scaled(int value4k, int minVal, int maxVal) const {
        return UIScale::scaled(value4k, minVal, maxVal, const_cast<ResponsiveWidget*>(this));
    }

private slots:
    void onResizeTimeout() {
        updateScaling();
    }

    void onScreenChanged(QScreen* screen) {
        Q_UNUSED(screen);
        // Screen changed - trigger rescale
        QTimer::singleShot(100, this, &ResponsiveWidget::updateScaling);
    }

    void onScreenGeometryChanged(const QRect& geometry) {
        Q_UNUSED(geometry);
        // Resolution changed on current screen - trigger rescale
        QTimer::singleShot(100, this, &ResponsiveWidget::updateScaling);
    }

private:
    void connectScreenSignals() {
        // Only connect once
        if (m_screenConnected) return;
        
        QWindow* win = window()->windowHandle();
        if (win) {
            // When window moves to different screen (drag or hot-swap)
            connect(win, &QWindow::screenChanged, 
                    this, &ResponsiveWidget::onScreenChanged);
            
            // When current screen resolution changes
            if (win->screen()) {
                connect(win->screen(), &QScreen::geometryChanged,
                        this, &ResponsiveWidget::onScreenGeometryChanged);
            }
            
            m_screenConnected = true;
        }
    }

    QTimer* m_resizeTimer;
    bool m_screenConnected = false;
};