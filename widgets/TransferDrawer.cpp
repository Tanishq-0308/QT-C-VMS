#include "TransferDrawer.hpp"
#include "core/TransferManager.hpp"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString formatSize(qint64 bytes) {
    if (bytes >= (qint64(1) << 30))
        return QString::number(double(bytes) / (1 << 30), 'f', 2) + " GB";
    return QString::number(double(bytes) / (1 << 20), 'f', 0) + " MB";
}

QString formatDuration(qint64 seconds) {
    if (seconds >= 3600)
        return QString("%1h %2m").arg(seconds / 3600).arg((seconds % 3600) / 60);
    if (seconds >= 60)
        return QString("%1m %2s").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    return QString("%1s").arg(seconds);
}

} // namespace

TransferDrawer::TransferDrawer(TransferManager* manager, QWidget* parent)
    : QFrame(parent), m_manager(manager)
{
    setObjectName("TransferDrawer");
    setStyleSheet(R"(
        #TransferDrawer { background: #ffffff; border-left: 2px solid #c40000; }
        #TransferDrawer QLabel, #TransferDrawer QScrollArea,
        #TransferDrawer QScrollArea > QWidget > QWidget { background: transparent; }
        #TransferRow { background: #f5f6f8; border: 1px solid #dde0e5; border-radius: 8px; }
        QProgressBar { border: 1px solid #ced0d5; border-radius: 5px; background: #ffffff; text-align: center; }
        QProgressBar::chunk { background: #0055aa; border-radius: 4px; }
        QPushButton#DrawerAction { background: #003366; color: white; border-radius: 6px; font-weight: bold; }
        QPushButton#DrawerAction:hover { background: #004488; }
        QPushButton#DrawerAction:disabled { background: #b8c0cc; color: #eef1f5; }
        QPushButton#RowCancel { background: transparent; color: #c40000; border: 1px solid #c40000; border-radius: 6px; }
        QPushButton#DrawerClose { background: transparent; border: none; color: #333; }
    )");

    auto* layout = new QVBoxLayout(this);

    auto* header = new QHBoxLayout;
    m_titleLabel = new QLabel("USB Transfers");
    m_closeBtn = new QPushButton("✕");
    m_closeBtn->setObjectName("DrawerClose");
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    header->addWidget(m_titleLabel, 1);
    header->addWidget(m_closeBtn);
    layout->addLayout(header);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listContainer = new QWidget;
    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_emptyLabel = new QLabel("No transfers.\nSelect files and press \"Download Selected\".");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: #777;");
    m_listLayout->addWidget(m_emptyLabel);
    m_listLayout->addStretch();
    scroll->setWidget(listContainer);
    layout->addWidget(scroll, 1);

    auto* footer = new QHBoxLayout;
    m_cancelAllBtn = new QPushButton("Cancel all");
    m_clearBtn = new QPushButton("Clear finished");
    for (QPushButton* btn : {m_cancelAllBtn, m_clearBtn}) {
        btn->setObjectName("DrawerAction");
        btn->setCursor(Qt::PointingHandCursor);
        footer->addWidget(btn);
    }
    layout->addLayout(footer);

    connect(m_closeBtn, &QPushButton::clicked, this, &TransferDrawer::closeDrawer);
    connect(m_cancelAllBtn, &QPushButton::clicked, m_manager, &TransferManager::cancelAll);
    connect(m_clearBtn, &QPushButton::clicked, m_manager, &TransferManager::clearFinished);
    connect(m_manager, &TransferManager::jobAdded, this, &TransferDrawer::addRow);
    connect(m_manager, &TransferManager::jobChanged, this, &TransferDrawer::updateRow);
    connect(m_manager, &TransferManager::jobsRemoved, this, &TransferDrawer::rebuildRows);
    connect(m_manager, &TransferManager::activeCountChanged, this, [this](int active) {
        m_cancelAllBtn->setEnabled(active > 0);
    });

    m_anim = new QPropertyAnimation(this, "geometry", this);
    m_anim->setDuration(220);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_open)
            hide();
    });

    parent->installEventFilter(this);   // follow the parent's size
    m_cancelAllBtn->setEnabled(false);
    rebuildRows();
    hide();
}

void TransferDrawer::setTopOffset(int y) {
    m_topOffset = y;
    if (isVisible())
        setGeometry(m_open ? openGeometry() : closedGeometry());
}

QRect TransferDrawer::openGeometry() const {
    const QWidget* p = parentWidget();
    const int w = qBound(380, p->width() * 30 / 100, 900);
    return QRect(p->width() - w, m_topOffset, w, p->height() - m_topOffset);
}

QRect TransferDrawer::closedGeometry() const {
    QRect r = openGeometry();
    r.moveLeft(parentWidget()->width());
    return r;
}

void TransferDrawer::openDrawer() {
    if (m_open)
        return;
    m_open = true;
    applyScaling();
    m_manager->markFailuresSeen();
    qApp->installEventFilter(this);   // to close on a tap outside the drawer

    m_anim->stop();
    if (!isVisible())
        setGeometry(closedGeometry());
    show();
    raise();
    m_anim->setStartValue(geometry());
    m_anim->setEndValue(openGeometry());
    m_anim->start();
}

void TransferDrawer::closeDrawer() {
    if (!m_open)
        return;
    m_open = false;
    qApp->removeEventFilter(this);

    m_anim->stop();
    m_anim->setStartValue(geometry());
    m_anim->setEndValue(closedGeometry());
    m_anim->start();
}

void TransferDrawer::toggleDrawer() {
    if (m_open)
        closeDrawer();
    else
        openDrawer();
}

bool TransferDrawer::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        if (isVisible() && m_anim->state() != QAbstractAnimation::Running)
            setGeometry(m_open ? openGeometry() : closedGeometry());
        applyScaling();
    } else if (m_open && event->type() == QEvent::MouseButtonPress) {
        // Judged by where the tap landed, not by the receiver: a press the drawer's widgets
        // ignore propagates on to its parent, which is not "outside"
        auto* target = qobject_cast<QWidget*>(watched);
        const QPoint globalPos = static_cast<QMouseEvent*>(event)->globalPos();
        const bool inside = rect().contains(mapFromGlobal(globalPos));
        const bool onToggle = m_toggleWidget && m_toggleWidget->isVisible() &&
                              m_toggleWidget->rect().contains(m_toggleWidget->mapFromGlobal(globalPos));
        // Only taps in this window count: dialogs opened from the drawer's own actions don't
        if (!inside && !onToggle && target && target->window() == window())
            closeDrawer();
    }
    return QFrame::eventFilter(watched, event);
}

void TransferDrawer::applyScaling() {
    const QWidget* p = parentWidget();
    m_fontPx = qBound(14, p->height() * 16 / 1000, 30);
    const int pad = m_fontPx;
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(pad * 2 / 3);
    m_listLayout->setSpacing(pad * 2 / 3);

    m_titleLabel->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #c40000;")
                                    .arg(m_fontPx * 3 / 2));
    m_closeBtn->setStyleSheet(QString("font-size: %1px;").arg(m_fontPx * 3 / 2));
    m_emptyLabel->setStyleSheet(QString("color: #777; font-size: %1px; padding: %2px;")
                                    .arg(m_fontPx).arg(pad * 2));
    const QString actionStyle = QString("font-size: %1px; padding: %2px %3px;")
                                    .arg(m_fontPx).arg(pad / 2).arg(pad);
    m_cancelAllBtn->setStyleSheet(actionStyle);
    m_clearBtn->setStyleSheet(actionStyle);

    for (const Row& row : m_rows) {
        row.name->setStyleSheet(QString("font-size: %1px; font-weight: bold;").arg(m_fontPx));
        row.bar->setFixedHeight(m_fontPx + 4);
        row.bar->setStyleSheet(QString("font-size: %1px;").arg(m_fontPx - 3));
        row.cancel->setStyleSheet(QString("font-size: %1px; padding: 2px %2px;")
                                      .arg(m_fontPx - 2).arg(pad / 2));
    }
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
        updateRow(it.key());
}

void TransferDrawer::addRow(int id) {
    const TransferManager::Job* job = m_manager->job(id);
    if (!job || m_rows.contains(id))
        return;

    Row row;
    row.widget = new QWidget;
    row.widget->setObjectName("TransferRow");
    row.widget->setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(row.widget);

    auto* top = new QHBoxLayout;
    row.name = new QLabel(job->fileName);
    row.name->setTextInteractionFlags(Qt::NoTextInteraction);
    row.cancel = new QPushButton("Cancel");
    row.cancel->setObjectName("RowCancel");
    row.cancel->setCursor(Qt::PointingHandCursor);
    top->addWidget(row.name, 1);
    top->addWidget(row.cancel);
    v->addLayout(top);

    row.bar = new QProgressBar;
    row.bar->setRange(0, 1000);
    row.bar->setTextVisible(false);
    v->addWidget(row.bar);

    row.status = new QLabel;
    row.status->setWordWrap(true);
    v->addWidget(row.status);

    connect(row.cancel, &QPushButton::clicked, this, [this, id]() { m_manager->cancel(id); });

    // Before the trailing stretch
    m_listLayout->insertWidget(m_listLayout->count() - 1, row.widget);
    m_rows.insert(id, row);
    applyScaling();
    updateEmptyState();
}

void TransferDrawer::updateRow(int id) {
    const TransferManager::Job* job = m_manager->job(id);
    auto it = m_rows.find(id);
    if (!job || it == m_rows.end())
        return;
    const Row& row = it.value();

    const qint64 size = qMax<qint64>(1, job->size);
    row.bar->setValue(int(qMin<qint64>(1000, job->bytesDone * 1000 / size)));

    QString text;
    QString color = "#444";
    switch (job->state) {
    case TransferManager::State::Queued:
        text = "Waiting… · " + formatSize(job->size);
        break;
    case TransferManager::State::Copying:
        if (job->bytesDone >= job->size) {
            text = "Finishing (writing to the stick)…";
        } else {
            text = formatSize(job->bytesDone) + " of " + formatSize(job->size);
            if (job->bytesPerSecond > 0) {
                text += QString(" · %1 MB/s").arg(job->bytesPerSecond / (1 << 20), 0, 'f', 1);
                const qint64 left = qint64(double(job->size - job->bytesDone) / job->bytesPerSecond);
                text += " · " + formatDuration(left) + " left";
            }
        }
        color = "#0055aa";
        break;
    case TransferManager::State::Done:
        text = "✓ Copied · " + formatSize(job->size);
        color = "#1a7f37";
        break;
    case TransferManager::State::Failed:
        text = "✗ Failed: " + job->error;
        color = "#c40000";
        break;
    case TransferManager::State::Cancelled:
        text = "Cancelled";
        color = "#777";
        break;
    }
    row.status->setText(text);
    row.status->setStyleSheet(QString("font-size: %1px; color: %2;").arg(m_fontPx - 2).arg(color));

    const bool active = job->state == TransferManager::State::Queued ||
                        job->state == TransferManager::State::Copying;
    row.cancel->setVisible(active);
    row.bar->setVisible(job->state != TransferManager::State::Failed &&
                        job->state != TransferManager::State::Cancelled);
    if (!active)
        m_clearBtn->setEnabled(true);
}

void TransferDrawer::rebuildRows() {
    for (const Row& row : m_rows)
        row.widget->deleteLater();
    m_rows.clear();
    for (const TransferManager::Job& job : m_manager->jobs())
        addRow(job.id);
    updateEmptyState();
}

void TransferDrawer::updateEmptyState() {
    const bool empty = m_rows.isEmpty();
    m_emptyLabel->setVisible(empty);
    bool anyFinished = false;
    for (const TransferManager::Job& job : m_manager->jobs()) {
        if (job.state != TransferManager::State::Queued && job.state != TransferManager::State::Copying)
            anyFinished = true;
    }
    m_clearBtn->setEnabled(anyFinished);
}
