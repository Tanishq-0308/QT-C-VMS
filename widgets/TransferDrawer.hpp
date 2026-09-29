#pragma once

#include <QFrame>
#include <QMap>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class QPropertyAnimation;
class TransferManager;

// Panel that slides in from the right edge of its parent and lists the USB transfers of a
// TransferManager: one row per file with progress, speed, state and a cancel button.
// It never blocks the rest of the app; tapping outside it closes it.
class TransferDrawer : public QFrame {
    Q_OBJECT

public:
    TransferDrawer(TransferManager* manager, QWidget* parent);

    bool isOpen() const { return m_open; }
    // Space at the top of the parent the drawer must leave uncovered (the top bar)
    void setTopOffset(int y);
    // A tap on this widget (the button that toggles the drawer) doesn't count as "outside"
    void setToggleWidget(QWidget* widget) { m_toggleWidget = widget; }

public slots:
    void openDrawer();
    void closeDrawer();
    void toggleDrawer();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        QWidget* widget = nullptr;
        QLabel* name = nullptr;
        QProgressBar* bar = nullptr;
        QLabel* status = nullptr;
        QPushButton* cancel = nullptr;
    };

    void addRow(int id);
    void updateRow(int id);
    void rebuildRows();
    void updateEmptyState();
    void applyScaling();
    QRect openGeometry() const;
    QRect closedGeometry() const;

    TransferManager* m_manager;
    QWidget* m_toggleWidget = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QPushButton* m_cancelAllBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QPropertyAnimation* m_anim = nullptr;
    QMap<int, Row> m_rows;
    int m_topOffset = 0;
    int m_fontPx = 16;
    bool m_open = false;
};
