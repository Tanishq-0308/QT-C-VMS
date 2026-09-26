
#include "SurgeryRecordingPage.hpp"
#include "../EditSurgeryDialog/EditSurgeryDialog.hpp"
#include <QPixmap>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimediaWidgets/QVideoWidget>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QDialog>
#include <QUrl>
#include <QSlider>
#include <QListWidget>
#include <QDesktopServices>
#include <QCheckBox>
#include <QTimer>
#include <QMessageBox>
#include <QInputDialog>
#include <QFile>
#include <QDir>
#include "ClickableSlider.hpp"
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <memory>
#include <unistd.h>

namespace {

struct UsbCopyResult {
    int succeeded = 0;
    QStringList failed;
    bool cancelled = false;
};

// Runs on a worker thread. Each file is written to a temporary file next to the destination,
// flushed to the device and then renamed over the destination, so an existing copy is only
// replaced by a complete one (pulling the stick mid-copy never destroys a previous copy).
// Data is synced every kSyncChunk bytes so progress reflects what is really on the device:
// slow USB 2.0 sticks otherwise absorb the whole file into the page cache instantly and then
// sit in one multi-minute fdatasync with the progress bar frozen.
UsbCopyResult copyFilesToUsb(const QStringList &sources, const QString &destDir,
                             std::shared_ptr<std::atomic<qint64>> bytesDone,
                             std::shared_ptr<std::atomic<bool>> cancelled)
{
    constexpr qint64 kSyncChunk = 16 << 20;
    UsbCopyResult result;
    QByteArray buffer(4 << 20, Qt::Uninitialized);

    for (const QString &sourcePath : sources) {
        QFile in(sourcePath);
        QSaveFile out(destDir + "/" + QFileInfo(sourcePath).fileName());
        bool ok = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly);
        qint64 unsynced = 0;

        while (ok && !in.atEnd() && !cancelled->load()) {
            const qint64 n = in.read(buffer.data(), buffer.size());
            ok = n >= 0 && out.write(buffer.constData(), n) == n;
            unsynced += qMax<qint64>(n, 0);
            if (ok && unsynced >= kSyncChunk) {
                ok = out.flush() && ::fdatasync(out.handle()) == 0;
                *bytesDone += unsynced;
                unsynced = 0;
            }
        }

        if (cancelled->load()) {
            out.cancelWriting();
            result.cancelled = true;
            break;
        }

        ok = ok && out.flush() && ::fdatasync(out.handle()) == 0 && out.commit();
        *bytesDone += unsynced;
        if (!ok) {
            out.cancelWriting();
            result.failed << QFileInfo(sourcePath).fileName();
            qWarning() << "Failed to copy:" << sourcePath << in.errorString() << out.errorString();
        } else {
            result.succeeded++;
        }
    }
    return result;
}

} // namespace

// ============== Responsive Helper Methods ==============

int SurgeryRecordingPage::percentWidth(int percent, int minVal, int maxVal) {
    int val = width() * percent / 100;
    return qBound(minVal, val, maxVal);
}

int SurgeryRecordingPage::percentHeight(int percent, int minVal, int maxVal) {
    int val = height() * percent / 100;
    return qBound(minVal, val, maxVal);
}

int SurgeryRecordingPage::scaled(int base4kValue, int minVal, int maxVal) {
    QScreen *screen = QGuiApplication::primaryScreen();
    int screenWidth = screen ? screen->geometry().width() : 3840;
    int val = base4kValue * screenWidth / 3840;
    return qBound(minVal, val, maxVal);
}

// ============== Event Handlers ==============

void SurgeryRecordingPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateScaling();
}

void SurgeryRecordingPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(50, this, [this]() {
        updateScaling();
    });
}

// ============== Constructor ==============

SurgeryRecordingPage::SurgeryRecordingPage(const QString &patientId, int surgeryId, QWidget *parent) 
    : QWidget(parent)
    , videoSection(nullptr)
    , snapshotSection(nullptr)
    , videoGrid(nullptr)
    , snapshotGrid(nullptr)
    , videoSectionTitle(nullptr)
    , snapshotSectionTitle(nullptr)
{
    m_patientId = patientId;
    m_surgeryId = surgeryId;
    
    setupUI();
    applyStyles();
    loadSurgeryDetails();
}

void SurgeryRecordingPage::setupUI() {
    mainLayout = new QVBoxLayout(this);

    // Go Back button
    QHBoxLayout *topBtnLayout = new QHBoxLayout;
    goBackBtn = new QPushButton("Go Back");
    topBtnLayout->addWidget(goBackBtn);
    topBtnLayout->addStretch();
    mainLayout->addLayout(topBtnLayout);

    // Title
    titleLabel = new QLabel("Surgery Details");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Surgery info section
    mainLayout->addWidget(createSurgeryInfoSection());

    // Action buttons
    QHBoxLayout *actionBtnLayout = new QHBoxLayout;
    actionBtnLayout->setAlignment(Qt::AlignCenter);

    editBtn = new QPushButton("Edit Surgery");
    reportBtn = new QPushButton("Generate Report");
    recordBtn = new QPushButton("Start Recording");

    for (QPushButton *btn : {editBtn, reportBtn, recordBtn}) {
        btn->setCursor(Qt::PointingHandCursor);
        actionBtnLayout->addWidget(btn);
    }
    mainLayout->addLayout(actionBtnLayout);

    // Recordings Section Title
    recTitle = new QLabel("Recordings");
    mainLayout->addWidget(recTitle);

    // Create recording sections
    videoSection = createRecordingSection("Videos", "recordings");
    snapshotSection = createRecordingSection("Snapshots", "snapshots");

    mainLayout->addWidget(videoSection);
    mainLayout->addWidget(snapshotSection);

    // Download & Delete buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    
    downloadBtn = new QPushButton("Download Selected");
    downloadBtn->setCursor(Qt::PointingHandCursor);
    buttonLayout->addWidget(downloadBtn);
    
    deleteBtn = new QPushButton("Delete Selected");
    deleteBtn->setCursor(Qt::PointingHandCursor);
    buttonLayout->addWidget(deleteBtn);
    
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    // Signal Connections
    connect(goBackBtn, &QPushButton::clicked, this, [=]() {
        emit goBackRequested();
    });
    
    connect(editBtn, &QPushButton::clicked, this, [=]() {
        EditSurgeryDialog *dialog = new EditSurgeryDialog(m_surgeryId, this);
        if (dialog->exec() == QDialog::Accepted) {
            loadSurgeryDetails();
        }
        dialog->deleteLater();
    });

    connect(reportBtn, &QPushButton::clicked, this, &SurgeryRecordingPage::generateReport);

    connect(recordBtn, &QPushButton::clicked, this, [=]() {
        emit goToRecordingPage(m_patientId, m_surgeryId);
    });

    connect(downloadBtn, &QPushButton::clicked, this, &SurgeryRecordingPage::downloadSelectedFiles);
    connect(deleteBtn, &QPushButton::clicked, this, &SurgeryRecordingPage::deleteSelectedFiles);
}

void SurgeryRecordingPage::applyStyles() {
    // Base styles (colors, non-scaling properties)
    goBackBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #003366;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 8px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #004488;"
        "}"
    );

    titleLabel->setStyleSheet("font-weight: bold; color: #c40000; font-size: 24px; padding: 10px");
    recTitle->setStyleSheet("font-weight: bold; color: red;");

    QString actionBtnStyle = 
        "QPushButton {"
        "   background-color: #c80000;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #e00000;"
        "}";

    editBtn->setStyleSheet(actionBtnStyle);
    reportBtn->setStyleSheet(actionBtnStyle);
    recordBtn->setStyleSheet(actionBtnStyle);

    downloadBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #0055aa;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #0066cc;"
        "}"
    );
    
    deleteBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #cc0000;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #ee0000;"
        "}"
    );
}

void SurgeryRecordingPage::updateScaling() {
    if (width() < 100 || height() < 100) return;
    
    // Skip if size hasn't changed much
    int widthDiff = qAbs(width() - m_lastWidth);
    int heightDiff = qAbs(height() - m_lastHeight);
    if (widthDiff < 20 && heightDiff < 20 && m_lastWidth > 0) {
        return;
    }
    m_lastWidth = width();
    m_lastHeight = height();

    // ============== Layout spacing/margins ==============
    int layoutSpacing = percentHeight(2, 15, 30);
    int marginH = percentWidth(2, 20, 50);
    int marginV = percentHeight(2, 15, 40);
    mainLayout->setSpacing(layoutSpacing);
    mainLayout->setContentsMargins(marginH, marginV, marginH, marginV);

    // ============== Go Back Button ==============
    int goBackFontSize = percentHeight(1.8, 14, 20);
    int goBackPadding = percentHeight(1, 8, 16);
    goBackBtn->setStyleSheet(
        QString("QPushButton {"
                "   background-color: #003366;"
                "   color: white;"
                "   border-radius: 8px;"
                "   font-size: %1px;"
                "   padding: %2px %3px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #004488;"
                "}")
        .arg(goBackFontSize)
        .arg(goBackPadding)
        .arg(goBackPadding + 5)
    );

    // ============== Title ==============
    int titleFontSize = percentHeight(2.8, 18, 30);
    titleLabel->setStyleSheet(
        QString("font-size: %1px; font-weight: bold; color: #c40000;")
        .arg(titleFontSize)
    );

    // ============== Surgery Info Labels ==============
    int labelFontSize = percentHeight(2, 12, 20);
    QString labelStyle = QString("font-size: %1px;").arg(labelFontSize);
    
    QLabel* infoLabels[] = {
        surgeonNameLabel, surgeryTypeLabel, bodyPartLabel, dateLabel,
        additionalSurgeonLabel, anesthesiologistLabel, surgicalHistoryLabel, theatreNumberLabel
    };
    for (QLabel* label : infoLabels) {
        if (label) label->setStyleSheet(labelStyle);
    }

    // ============== Action Buttons ==============
    int actionFontSize = percentHeight(1.8, 14, 24);
    int actionPadding = percentHeight(1.5, 12, 24);
    int actionMargin = percentWidth(1, 10, 25);
    
    QString actionStyle = QString(
        "QPushButton {"
        "   background-color: #c80000;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "   font-size: %1px;"
        "   padding: %2px 10px;"
        "   margin: %3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #e00000;"
        "}")
        .arg(actionFontSize)
        .arg(actionPadding)
        .arg(actionMargin);

    editBtn->setStyleSheet(actionStyle);
    reportBtn->setStyleSheet(actionStyle);
    recordBtn->setStyleSheet(actionStyle);

    // ============== Recordings Title ==============
    int recTitleSize = percentHeight(2.5, 16, 26);
    recTitle->setStyleSheet(
        QString("font-size: %1px; font-weight: bold; color: red;")
        .arg(recTitleSize)
    );

    // ============== Section Titles ==============
    int sectionTitleSize = percentHeight(2, 14, 22);
    if (videoSectionTitle) {
        videoSectionTitle->setStyleSheet(
            QString("font-size: %1px; font-weight: bold;").arg(sectionTitleSize)
        );
    }
    if (snapshotSectionTitle) {
        snapshotSectionTitle->setStyleSheet(
            QString("font-size: %1px; font-weight: bold;").arg(sectionTitleSize)
        );
    }

    // ============== Download & Delete Buttons ==============
    int dlFontSize = percentHeight(1.9, 12, 20);
    int dlPadding = percentHeight(1.5, 12, 24);
    downloadBtn->setStyleSheet(
        QString("QPushButton {"
                "   background-color: #0055aa;"
                "   color: white;"
                "   font-weight: bold;"
                "   border-radius: 5px;"
                "   font-size: %1px;"
                "   padding: %2px 10px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #0066cc;"
                "}")
        .arg(dlFontSize)
        .arg(dlPadding)
    );
    
    deleteBtn->setStyleSheet(
        QString("QPushButton {"
                "   background-color: #cc0000;"
                "   color: white;"
                "   font-weight: bold;"
                "   border-radius: 5px;"
                "   font-size: %1px;"
                "   padding: %2px 10px;"
                "}"
                "QPushButton:hover {"
                "   background-color: #ee0000;"
                "}")
        .arg(dlFontSize)
        .arg(dlPadding)
    );

    // ============== Grid Cards ==============
    int cardW = percentWidth(12, 180, 250);
    int cardH = percentHeight(20, 150, 180);
    int thumbW = cardW - 10;
    int thumbH = cardH - 70;  // More space for delete button
    
    int fileLabelFontSize = percentHeight(1.5, 10, 16);
    int fileLabelPadding = percentHeight(0.5, 3, 6);
    int fileLabelRadius = percentHeight(0.4, 3, 5);
    int checkBoxSize = percentHeight(2, 16, 24);
    int gridSpacing = percentWidth(1, 10, 20);
    int deleteBtnSize = percentHeight(2.5, 20, 30);

    // Update video grid
    if (videoGrid) {
        videoGrid->setSpacing(gridSpacing);
    }
    
    // Update snapshot grid
    if (snapshotGrid) {
        snapshotGrid->setSpacing(gridSpacing);
    }

    // Update video cards
    for (CardWidgets& cw : videoCards) {
        if (cw.card) {
            cw.card->setFixedSize(cardW, cardH);
        }
        if (cw.thumbnail) {
            QPixmap pix;
            if (cw.isVideo) {
                pix = QPixmap(":/assets/icons/play-button.png");
            } else if (QFileInfo::exists(cw.filePath)) {
                pix = QPixmap(cw.filePath);
            } else {
                pix = QPixmap(":/icons/sample-thumb.png");
            }
            cw.thumbnail->setPixmap(pix.scaled(thumbW, thumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        if (cw.fileLabel) {
            cw.fileLabel->setStyleSheet(
                QString("font-size: %1px; background-color: #003366; color: white; "
                        "padding: %2px; border-radius: %3px;")
                .arg(fileLabelFontSize).arg(fileLabelPadding).arg(fileLabelRadius)
            );
        }
        if (cw.checkBox) {
            cw.checkBox->setStyleSheet(
                QString("QCheckBox::indicator { width: %1px; height: %1px; }")
                .arg(checkBoxSize)
            );
        }
        if (cw.deleteBtn) {
            cw.deleteBtn->setFixedSize(deleteBtnSize, deleteBtnSize);
            cw.deleteBtn->setStyleSheet(
                QString("QPushButton {"
                        "   background-color: #cc0000;"
                        "   color: white;"
                        "   border: none;"
                        "   border-radius: %1px;"
                        "   font-size: %2px;"
                        "   font-weight: bold;"
                        "}"
                        "QPushButton:hover {"
                        "   background-color: #ff0000;"
                        "}")
                .arg(deleteBtnSize / 2)
                .arg(deleteBtnSize / 2)
            );
        }
    }

    // Update snapshot cards
    for (CardWidgets& cw : snapshotCards) {
        if (cw.card) {
            cw.card->setFixedSize(cardW, cardH);
        }
        if (cw.thumbnail) {
            QPixmap pix;
            if (cw.isVideo) {
                pix = QPixmap(":/assets/icons/play-button.png");
            } else if (QFileInfo::exists(cw.filePath)) {
                pix = QPixmap(cw.filePath);
            } else {
                pix = QPixmap(":/icons/sample-thumb.png");
            }
            cw.thumbnail->setPixmap(pix.scaled(thumbW, thumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        if (cw.fileLabel) {
            cw.fileLabel->setStyleSheet(
                QString("font-size: %1px; background-color: #003366; color: white; "
                        "padding: %2px; border-radius: %3px;")
                .arg(fileLabelFontSize).arg(fileLabelPadding).arg(fileLabelRadius)
            );
        }
        if (cw.checkBox) {
            cw.checkBox->setStyleSheet(
                QString("QCheckBox::indicator { width: %1px; height: %1px; }")
                .arg(checkBoxSize)
            );
        }
        if (cw.deleteBtn) {
            cw.deleteBtn->setFixedSize(deleteBtnSize, deleteBtnSize);
            cw.deleteBtn->setStyleSheet(
                QString("QPushButton {"
                        "   background-color: #cc0000;"
                        "   color: white;"
                        "   border: none;"
                        "   border-radius: %1px;"
                        "   font-size: %2px;"
                        "   font-weight: bold;"
                        "}"
                        "QPushButton:hover {"
                        "   background-color: #ff0000;"
                        "}")
                .arg(deleteBtnSize / 2)
                .arg(deleteBtnSize / 2)
            );
        }
    }

    // Rebuild grid layouts if needed
    rebuildGridLayout(videoGrid, videoCards);
    rebuildGridLayout(snapshotGrid, snapshotCards);
    
    m_stylesApplied = true;
}

void SurgeryRecordingPage::rebuildGridLayout(QGridLayout* grid, const QList<CardWidgets>& cards) {
    if (!grid || cards.isEmpty()) return;
    
    int cardW = percentWidth(14, 180, 300);
    int gridSpacing = percentWidth(1, 10, 20);
    int availableWidth = width() - 80;
    int cols = qMax(3, availableWidth / (cardW + gridSpacing));
    
    // Remove all items from grid (but don't delete widgets)
    while (grid->count() > 0) {
        QLayoutItem* item = grid->takeAt(0);
        delete item;
    }
    
    // Re-add widgets in new positions
    for (int i = 0; i < cards.size(); i++) {
        if (cards[i].card) {
            grid->addWidget(cards[i].card, i / cols, i % cols);
        }
    }
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
}

QWidget* SurgeryRecordingPage::createSurgeryInfoSection() {
    QWidget *container = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setAlignment(Qt::AlignCenter);

    // Left info
    QVBoxLayout *leftLayout = new QVBoxLayout;
    surgeonNameLabel = new QLabel("Surgeon Name : ");
    surgeryTypeLabel = new QLabel("Surgery Type : ");
    bodyPartLabel = new QLabel("Body Part : ");
    dateLabel = new QLabel("Date : ");

    leftLayout->addWidget(surgeonNameLabel);
    leftLayout->addWidget(surgeryTypeLabel);
    leftLayout->addWidget(bodyPartLabel);
    leftLayout->addWidget(dateLabel);
    layout->addLayout(leftLayout);

    // Right info
    QVBoxLayout *rightLayout = new QVBoxLayout;
    additionalSurgeonLabel = new QLabel("Additional Surgeon :");
    anesthesiologistLabel = new QLabel("Anesthesiologist :");
    surgicalHistoryLabel = new QLabel("Surgical History Details :");
    theatreNumberLabel = new QLabel("Operation Theatre Number :");

    rightLayout->addWidget(additionalSurgeonLabel);
    rightLayout->addWidget(anesthesiologistLabel);
    rightLayout->addWidget(surgicalHistoryLabel);
    rightLayout->addWidget(theatreNumberLabel);
    layout->addLayout(rightLayout);

    return container;
}

QWidget* SurgeryRecordingPage::createRecordingSection(const QString &title, const QString &tableName) {
    QWidget *section = new QWidget;
    QVBoxLayout *sectionLayout = new QVBoxLayout(section);

    QLabel *sectionTitle = new QLabel(title);
    sectionLayout->addWidget(sectionTitle);
    
    // Store section title reference
    if (tableName == "recordings") {
        videoSectionTitle = sectionTitle;
    } else {
        snapshotSectionTitle = sectionTitle;
    }

    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);

    QWidget *gridContainer = new QWidget;
    QGridLayout *gridLayout = new QGridLayout(gridContainer);
    
    // Store grid reference
    if (tableName == "recordings") {
        videoGrid = gridLayout;
    } else {
        snapshotGrid = gridLayout;
    }

    QSqlQuery query;
    QString queryStr = QString("SELECT id, file_path FROM %1 WHERE patient_id = :patientId AND surgery_id = :surgeryId").arg(tableName);
    query.prepare(queryStr);
    query.bindValue(":patientId", m_patientId);
    query.bindValue(":surgeryId", m_surgeryId);

    if (query.exec()) {
        int i = 0;
        int cols = 5;

        while (query.next()) {
            int fileId = query.value(0).toInt();
            QString filePath = query.value(1).toString();
            QString ext = QFileInfo(filePath).suffix().toLower();
            bool isVideo = (ext == "mp4" || ext == "avi" || ext == "mov");

            QWidget *card = new QWidget;
            QVBoxLayout *cardLayout = new QVBoxLayout(card);
            cardLayout->setAlignment(Qt::AlignCenter);
            cardLayout->setContentsMargins(5, 5, 5, 5);
            cardLayout->setSpacing(3);

            ClickableLabel *thumbnail = new ClickableLabel;
            thumbnail->setFilePath(filePath);
            thumbnail->setStyleSheet("background-color: #ddd; border: 1px solid #ccc;");
            thumbnail->setAlignment(Qt::AlignCenter);
            
            connect(thumbnail, &ClickableLabel::clicked, this, [=]() {
                handleThumbnailClick(filePath, fileId);
            });

            // Horizontal layout for checkbox + filename + delete button
            QWidget *labelContainer = new QWidget;
            QHBoxLayout *labelLayout = new QHBoxLayout(labelContainer);
            labelLayout->setContentsMargins(0, 0, 0, 0);
            labelLayout->setSpacing(3);

            QCheckBox *checkBox = new QCheckBox;
            connect(checkBox, &QCheckBox::stateChanged, this, [=](int state) {
                if (tableName == "snapshots") {
                    if (state == Qt::Checked)
                        selectedSnapshots.insert(filePath);
                    else
                        selectedSnapshots.remove(filePath);
                } else {
                    if (state == Qt::Checked)
                        selectedRecordings.insert(filePath);
                    else
                        selectedRecordings.remove(filePath);
                }
            });

            if (tableName == "snapshots") {
                snapshotCheckBoxes.append(checkBox);
                snapshotPathToId[filePath] = fileId;
            } else {
                recordingCheckBoxes.append(checkBox);
                recordingPathToId[filePath] = fileId;
            }

            QLabel *fileLabel = new QLabel(QFileInfo(filePath).fileName());
            fileLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            
            // Delete button for this card
            QPushButton *delBtn = new QPushButton("🗑");
            delBtn->setCursor(Qt::PointingHandCursor);
            delBtn->setToolTip("Delete this file");
            connect(delBtn, &QPushButton::clicked, this, [=]() {
                deleteSingleFile(fileId, filePath, isVideo);
            });

            labelLayout->addWidget(checkBox);
            labelLayout->addWidget(fileLabel, 1);
            labelLayout->addWidget(delBtn);

            cardLayout->addWidget(thumbnail);
            cardLayout->addWidget(labelContainer);

            gridLayout->addWidget(card, i / cols, i % cols);
            gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            
            // Store card reference for responsive updates
            CardWidgets cw;
            cw.card = card;
            cw.thumbnail = thumbnail;
            cw.fileLabel = fileLabel;
            cw.checkBox = checkBox;
            cw.deleteBtn = delBtn;
            cw.filePath = filePath;
            cw.fileId = fileId;
            cw.isVideo = isVideo;
            
            if (tableName == "recordings") {
                videoCards.append(cw);
            } else {
                snapshotCards.append(cw);
            }
            
            ++i;
        }
    } else {
        qDebug() << "Failed to load" << tableName << "files:" << query.lastError().text();
    }

    scrollArea->setWidget(gridContainer);
    sectionLayout->addWidget(scrollArea);
    return section;
}

void SurgeryRecordingPage::refreshRecordings() {
    // Clear existing cards
    videoCards.clear();
    snapshotCards.clear();
    snapshotCheckBoxes.clear();
    recordingCheckBoxes.clear();
    selectedSnapshots.clear();
    selectedRecordings.clear();
    snapshotPathToId.clear();
    recordingPathToId.clear();
    
    // Remove old sections
    if (videoSection) {
        mainLayout->removeWidget(videoSection);
        videoSection->deleteLater();
        videoSection = nullptr;
        videoGrid = nullptr;
        videoSectionTitle = nullptr;
    }
    if (snapshotSection) {
        mainLayout->removeWidget(snapshotSection);
        snapshotSection->deleteLater();
        snapshotSection = nullptr;
        snapshotGrid = nullptr;
        snapshotSectionTitle = nullptr;
    }
    
    // Recreate sections
    videoSection = createRecordingSection("Videos", "recordings");
    snapshotSection = createRecordingSection("Snapshots", "snapshots");
    
    // Insert before download button layout (which is at the end)
    mainLayout->insertWidget(mainLayout->count() - 1, videoSection);
    mainLayout->insertWidget(mainLayout->count() - 1, snapshotSection);
    
    // Trigger scaling update
    m_lastWidth = 0;
    m_lastHeight = 0;
    updateScaling();
}

// ============== DELETE FUNCTIONALITY ==============

bool SurgeryRecordingPage::deleteFromDatabase(int fileId, bool isVideo) {
    QSqlQuery query;
    
    // First delete related comments
    if (isVideo) {
        query.prepare("DELETE FROM comments_video WHERE video_id = :fileId");
    } else {
        query.prepare("DELETE FROM comments_image WHERE image_id = :fileId");
    }
    query.bindValue(":fileId", fileId);
    
    if (!query.exec()) {
        qDebug() << "Failed to delete comments:" << query.lastError().text();
        // Continue anyway - comments might not exist
    }
    
    // Now delete the recording/snapshot record
    QString tableName = isVideo ? "recordings" : "snapshots";
    query.prepare(QString("DELETE FROM %1 WHERE id = :fileId").arg(tableName));
    query.bindValue(":fileId", fileId);
    
    if (!query.exec()) {
        qDebug() << "Failed to delete from" << tableName << ":" << query.lastError().text();
        return false;
    }
    
    qDebug() << "✅ Deleted from database:" << tableName << "id=" << fileId;
    return true;
}

bool SurgeryRecordingPage::deleteFileFromStorage(const QString& filePath) {
    QFile file(filePath);
    if (file.exists()) {
        if (file.remove()) {
            qDebug() << "✅ Deleted file:" << filePath;
            return true;
        } else {
            qDebug() << "❌ Failed to delete file:" << filePath;
            return false;
        }
    } else {
        qDebug() << "⚠️ File not found (already deleted?):" << filePath;
        return true;  // Consider it success if file doesn't exist
    }
}

void SurgeryRecordingPage::deleteSingleFile(int fileId, const QString& filePath, bool isVideo) {
    QString fileType = isVideo ? "recording" : "snapshot";
    QString fileName = QFileInfo(filePath).fileName();
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Delete",
        QString("Are you sure you want to delete this %1?\n\n%2\n\nThis action cannot be undone.")
            .arg(fileType, fileName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        bool dbSuccess = deleteFromDatabase(fileId, isVideo);
        bool fileSuccess = deleteFileFromStorage(filePath);
        
        if (dbSuccess) {
            showToast(QString("🗑 %1 deleted successfully").arg(fileType.at(0).toUpper() + fileType.mid(1)));
            refreshRecordings();
        } else {
            QMessageBox::warning(this, "Error", "Failed to delete " + fileType + " from database.");
        }
    }
}

void SurgeryRecordingPage::deleteSelectedFiles() {
    if (selectedSnapshots.isEmpty() && selectedRecordings.isEmpty()) {
        QMessageBox::information(this, "No Selection", "Please select at least one file to delete.");
        return;
    }
    
    int totalCount = selectedSnapshots.size() + selectedRecordings.size();
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Delete",
        QString("Are you sure you want to delete %1 selected file(s)?\n\nThis action cannot be undone.")
            .arg(totalCount),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    int successCount = 0;
    int failCount = 0;
    
    // Delete selected snapshots
    for (const QString& filePath : selectedSnapshots) {
        int fileId = snapshotPathToId.value(filePath, -1);
        if (fileId != -1) {
            if (deleteFromDatabase(fileId, false) && deleteFileFromStorage(filePath)) {
                successCount++;
            } else {
                failCount++;
            }
        }
    }
    
    // Delete selected recordings
    for (const QString& filePath : selectedRecordings) {
        int fileId = recordingPathToId.value(filePath, -1);
        if (fileId != -1) {
            if (deleteFromDatabase(fileId, true) && deleteFileFromStorage(filePath)) {
                successCount++;
            } else {
                failCount++;
            }
        }
    }
    
    // Clear selections
    selectedSnapshots.clear();
    selectedRecordings.clear();
    
    if (successCount > 0) {
        showToast(QString("🗑 Deleted %1 file(s) successfully").arg(successCount));
    }
    
    if (failCount > 0) {
        QMessageBox::warning(this, "Partial Error", 
            QString("%1 file(s) could not be deleted.").arg(failCount));
    }
    
    // Refresh UI
    refreshRecordings();
}

// ============== EXISTING METHODS ==============

void SurgeryRecordingPage::loadSurgeryDetails() {
    QSqlQuery query;
    query.prepare("SELECT surgeon_name, surgery_type, body_part, surgery_date, "
                  "additional_surgeon, anesthesiologist, surgical_history, operation_theatre_number "
                  "FROM surgeries WHERE id = :id");
    query.bindValue(":id", m_surgeryId);

    if (query.exec() && query.next()) {
        QString surgeonName = query.value("surgeon_name").toString();
        QString surgeryType = query.value("surgery_type").toString();
        QString bodyPart = query.value("body_part").toString();
        QString date = query.value("surgery_date").toString();
        QString additionalSurgeon = query.value("additional_surgeon").toString();
        QString anesthesiologist = query.value("anesthesiologist").toString();
        QString surgicalHistory = query.value("surgical_history").toString();
        QString theatreNumber = query.value("operation_theatre_number").toString();

        surgeonNameLabel->setText("Surgeon Name : <span style='color:grey; '>" + surgeonName + "</span>");
        surgeryTypeLabel->setText("Surgery Type : <span style='color:grey; '>" + surgeryType + "</span>");
        bodyPartLabel->setText("Body Part : <span style='color:grey; '>" + bodyPart + "</span>");
        dateLabel->setText("Date : <span style='color:grey; '>" + date + "</span>");
        additionalSurgeonLabel->setText("Additional Surgeon : <span style='color:grey; '>" + additionalSurgeon + "</span>");
        anesthesiologistLabel->setText("Anesthesiologist : <span style='color:grey; '>" + anesthesiologist + "</span>");
        surgicalHistoryLabel->setText("Surgical History Details : <span style='color:grey; '>" + surgicalHistory + "</span>");
        theatreNumberLabel->setText("Operation Theatre Number : <span style='color:grey; '>" + theatreNumber + "</span>");

        qDebug() << "Surgery details loaded successfully.";
    } else {
        qDebug() << "Failed to fetch surgery: " << query.lastError().text();
    }
}

void SurgeryRecordingPage::handleThumbnailClick(const QString &filePath, int fileId) {
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();

    if (ext == "mp4" || ext == "avi" || ext == "mov") {
        QDialog *dialog = new QDialog(this);
        dialog->setWindowTitle("Video Preview");
        QScreen* screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->availableGeometry();
        dialog->resize(static_cast<int>(screenGeometry.width() * 0.9), 
                       static_cast<int>(screenGeometry.height() * 0.9));

        QHBoxLayout *dialogLayout = new QHBoxLayout(dialog);

        // Left side: video + controls + toggle button
        QWidget *leftSide = new QWidget;
        QVBoxLayout *videoMainLayout = new QVBoxLayout(leftSide);

        QVideoWidget *videoWidgett = new QVideoWidget(dialog);
        QMediaPlayer *player = new QMediaPlayer(dialog);
        player->setVideoOutput(videoWidgett);
        player->setMedia(QUrl::fromLocalFile(filePath));

        QHBoxLayout *controlsLayout = new QHBoxLayout();
        QPushButton *playPauseBtn = new QPushButton("Play", dialog);
        playPauseBtn->setStyleSheet("font-size:20px");
        ClickableSlider *positionSlider = new ClickableSlider(Qt::Horizontal, dialog);
        QLabel *timeLabel = new QLabel("00:00 / 00:00", dialog);

        controlsLayout->addWidget(playPauseBtn);
        controlsLayout->addWidget(positionSlider);
        controlsLayout->addWidget(timeLabel);

        if (videoWidgett) {
            videoMainLayout->addWidget(videoWidgett);
        }

        videoMainLayout->addLayout(controlsLayout);

        // Toggle button
        QPushButton *toggleCommentsBtn = new QPushButton("Show Comments", dialog);
        toggleCommentsBtn->setStyleSheet("font-size:20px");
        videoMainLayout->addWidget(toggleCommentsBtn);

        dialogLayout->addWidget(leftSide, 3);

        // Right side: comments panel (initially hidden)
        QWidget *rightPanel = new QWidget;
        rightPanel->setFixedWidth(400);
        rightPanel->setStyleSheet("background-color: #f9f9f9; border-left: 1px solid #ccc;");

        QVBoxLayout *commentLayout = new QVBoxLayout(rightPanel);
        commentLayout->setContentsMargins(10, 10, 10, 10);
        commentLayout->setSpacing(10);

        QLabel *commentsLabel = new QLabel("Comments:", rightPanel);
        commentsLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #003366;");
        commentLayout->addWidget(commentsLabel);

        QListWidget *commentList = new QListWidget(rightPanel);
        commentList->setStyleSheet(R"(
            QListWidget {
                background-color: #ffffff;
                border: 1px solid #ccc;
                font-size: 14px;
            }
            QListWidget::item {
                padding: 8px;
                border-bottom: 1px solid #eee;
            }
            QListWidget::item:selected {
                background-color: #e6f2ff;
            }
        )");
        commentLayout->addWidget(commentList);

        rightPanel->setVisible(false);
        dialogLayout->addWidget(rightPanel, 1);

        connect(toggleCommentsBtn, &QPushButton::clicked, [rightPanel, toggleCommentsBtn]() {
            bool currentlyVisible = rightPanel->isVisible();
            rightPanel->setVisible(!currentlyVisible);
            toggleCommentsBtn->setText(currentlyVisible ? "Show Comments" : "Hide Comments");
            toggleCommentsBtn->setStyleSheet("font-size: 20px");
        });

        QSqlQuery commentQuery;
        commentQuery.prepare("SELECT text FROM comments_video WHERE video_id = :fileId");
        commentQuery.bindValue(":fileId", fileId);

        if (commentQuery.exec()) {
            while (commentQuery.next()) {
                QString comment = commentQuery.value(0).toString();
                commentList->addItem(comment);
            }
        } else {
            qDebug() << "Failed to load comments:" << commentQuery.lastError().text();
        }

        connect(playPauseBtn, &QPushButton::clicked, [player, playPauseBtn]() {
            if (player->state() == QMediaPlayer::PlayingState) {
                player->pause();
                playPauseBtn->setText("Play");
            } else {
                player->play();
                playPauseBtn->setText("Pause");
            }
        });

        connect(player, &QMediaPlayer::durationChanged, [positionSlider](qint64 duration) {
            positionSlider->setRange(0, duration);
        });

        connect(player, &QMediaPlayer::positionChanged, [positionSlider, timeLabel, player](qint64 position) {
            if (!positionSlider->isSliderDown())
                positionSlider->setValue(position);

            auto formatTime = [](qint64 ms) {
                int sec = (ms / 1000) % 60;
                int min = (ms / 60000) % 60;
                int hrs = ms / 3600000;
                return hrs > 0
                    ? QString::asprintf("%d:%02d:%02d", hrs, min, sec)
                    : QString::asprintf("%02d:%02d", min, sec);
            };
            timeLabel->setText(formatTime(position) + " / " + formatTime(player->duration()));
        });

        connect(positionSlider, &QSlider::sliderMoved, player, &QMediaPlayer::setPosition);

        player->play();
        dialog->exec();

        player->stop();
        dialog->deleteLater(); // also deletes the player, video widget and controls (children)
    } else {
        // Image
        QDialog *dialog = new QDialog(this);
        dialog->setWindowTitle("Image Preview");

        QHBoxLayout *dialogLayout = new QHBoxLayout(dialog);

        QWidget *leftSide = new QWidget;
        QVBoxLayout *leftLayout = new QVBoxLayout(leftSide);

        QLabel *imageLabel = new QLabel(dialog);
        imageLabel->setAlignment(Qt::AlignCenter);

        QPixmap pix(filePath);
        QScreen *screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->availableGeometry();
        int maxWidth = screenGeometry.width() * 0.8;
        int maxHeight = screenGeometry.height() * 0.8;
        QPixmap scaledPix = pix.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imageLabel->setPixmap(scaledPix);

        leftLayout->addWidget(imageLabel);

        QHBoxLayout *buttonLayout = new QHBoxLayout;
        QPushButton *toggleCommentsBtn = new QPushButton("Show Comments", dialog);
        toggleCommentsBtn->setStyleSheet("font-size: 20px");
        QPushButton *addCommentBtn = new QPushButton("Add Comment", dialog);
        addCommentBtn->setStyleSheet("font-size: 20px");
        buttonLayout->addWidget(toggleCommentsBtn);
        buttonLayout->addWidget(addCommentBtn);
        leftLayout->addLayout(buttonLayout);

        dialogLayout->addWidget(leftSide, 3);

        QWidget *rightPanel = new QWidget;
        rightPanel->setFixedWidth(400);
        rightPanel->setStyleSheet("background-color: #f9f9f9; border-left: 1px solid #ccc;");
        QVBoxLayout *commentLayout = new QVBoxLayout(rightPanel);
        commentLayout->setContentsMargins(10, 10, 10, 10);
        commentLayout->setSpacing(10);

        QLabel *commentsLabel = new QLabel("Comments:", rightPanel);
        commentsLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #003366;");
        commentLayout->addWidget(commentsLabel);

        QListWidget *commentList = new QListWidget(rightPanel);
        commentList->setStyleSheet(R"(
            QListWidget {
                background-color: #ffffff;
                border: 1px solid #ccc;
                font-size: 14px;
            }
            QListWidget::item {
                padding: 8px;
                border-bottom: 1px solid #eee;
            }
            QListWidget::item:selected {
                background-color: #e6f2ff;
            }
        )");
        commentLayout->addWidget(commentList);

        rightPanel->setVisible(false);
        dialogLayout->addWidget(rightPanel, 1);

        QSqlQuery commentQuery;
        commentQuery.prepare("SELECT text FROM comments_image WHERE image_id = :imageId");
        commentQuery.bindValue(":imageId", fileId);
        if (commentQuery.exec()) {
            while (commentQuery.next()) {
                QString comment = commentQuery.value(0).toString();
                commentList->addItem(comment);
            }
        } else {
            qDebug() << "Failed to load image comments:" << commentQuery.lastError().text();
        }

        connect(toggleCommentsBtn, &QPushButton::clicked, [rightPanel, toggleCommentsBtn]() {
            bool currentlyVisible = rightPanel->isVisible();
            rightPanel->setVisible(!currentlyVisible);
            toggleCommentsBtn->setText(currentlyVisible ? "Show Comments" : "Hide Comments");
        });

        connect(addCommentBtn, &QPushButton::clicked, [=]() {
            bool ok;
            QString newComment = QInputDialog::getText(dialog, "Add Comment", "Enter your comment:", QLineEdit::Normal, "", &ok);
            if (ok && !newComment.trimmed().isEmpty()) {
                QSqlQuery insertQuery;
                insertQuery.prepare("INSERT INTO comments_image (image_id, text) VALUES (?, ?)");
                insertQuery.addBindValue(fileId);
                insertQuery.addBindValue(newComment.trimmed());

                if (insertQuery.exec()) {
                    commentList->addItem(newComment.trimmed());
                } else {
                    qDebug() << "Failed to insert image comment:" << insertQuery.lastError().text();
                }
            }
        });

        dialog->setLayout(dialogLayout);
        dialog->exec();
        dialog->deleteLater();
    }
}

void SurgeryRecordingPage::generateReport() {
    QJsonObject json;
    json["patient_id"] = m_patientId;
    json["surgery_id"] = m_surgeryId;

    QNetworkRequest request(QUrl("http://localhost:8001/generate-pdf"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(60000); // never wait forever on the report service

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    QNetworkReply *reply = manager->post(request, QJsonDocument(json).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            QJsonObject obj = doc.object();

            QString pdfPath = obj["pdf_path"].toString();
            qDebug() << "Generated PDF:" << pdfPath;

            emit openPdfReport(pdfPath);
            qDebug() << "Emitted:";

        } else {
            qDebug() << "Failed to generate PDF:" << reply->errorString();
            QMessageBox::information(this, "Warning", "Failed to Generate PDF.");
        }
        reply->deleteLater();
        manager->deleteLater(); // one manager per report request
    });
}

void SurgeryRecordingPage::downloadSelectedFiles() {
    if (selectedSnapshots.isEmpty() && selectedRecordings.isEmpty()) {
        QMessageBox::information(this, "No files", "Please select at least one file to download.");
        return;
    }

    QString basePath = "/media/brainwave";
    QDir mediaDir(basePath);
    QString destDir;

    if (mediaDir.exists()) {
        QStringList deviceDirs = mediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!deviceDirs.isEmpty()) {
            QString usbMountPath = basePath + "/" + deviceDirs.first();
            destDir = usbMountPath + "/SurgeryDownloads";
        }
    }

    if (destDir.isEmpty()) {
        QMessageBox::warning(this,
                             "No USB Device Found",
                             "Please connect a USB device before attempting to download the files.");
        qWarning() << "No USB mount found under:" << basePath;
        return;
    }

    if (!QDir().mkpath(destDir)) {
        QMessageBox::warning(this, "Download Error", "Could not create the folder on the USB device:\n" + destDir);
        return;
    }

    QStringList sources = selectedSnapshots.values();
    sources += selectedRecordings.values();
    qint64 totalBytes = 0;
    for (const QString &path : sources)
        totalBytes += QFileInfo(path).size();

    // Copy on a worker thread: multi-GB recordings must not freeze the UI (and the live view)
    auto bytesDone = std::make_shared<std::atomic<qint64>>(0);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    const int totalMb = int(qMax<qint64>(1, totalBytes >> 20));

    auto *progress = new QProgressDialog("Copying files to the USB device…", "Cancel", 0, totalMb, this);
    progress->setWindowTitle("Download");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    downloadBtn->setEnabled(false);

    auto *poll = new QTimer(progress);
    connect(poll, &QTimer::timeout, progress, [progress, bytesDone]() {
        progress->setValue(int(bytesDone->load() >> 20));
    });
    poll->start(200);

    // Cancel button, Esc and the window's close button all emit canceled(). The worker stops
    // after the chunk it is syncing and discards the partial file.
    connect(progress, &QProgressDialog::canceled, progress, [progress, poll, cancelled]() {
        cancelled->store(true);
        poll->stop();
        progress->hide();
    });

    auto *watcher = new QFutureWatcher<UsbCopyResult>(this);
    connect(watcher, &QFutureWatcher<UsbCopyResult>::finished, this, [this, watcher, progress]() {
        const UsbCopyResult resul*******cher->result();
        watcher->deleteLater();
        progress->deleteLater();
        downloadBtn->setEnabled(true);

        if (result.cancelled)
            showToast("Download cancelled (" + QString::number(result.succeeded) + " file(s) copied)");
        else if (result.succeeded > 0)
            showToast("Downloaded " + QString::number(result.succeeded) + " file(s) successfully");
        if (!result.failed.isEmpty()) {
            QMessageBox::warning(this, "Download Error",
                                 QString::number(result.failed.size()) + " file(s) could not be copied:\n" +
                                 result.failed.join("\n"));
        }
    });
    watcher->setFuture(QtConcurrent::run(copyFilesToUsb, sources, destDir, bytesDone, cancelled));

    selectedSnapshots.clear();
    selectedRecordings.clear();

    for (QCheckBox *cb : snapshotCheckBoxes) {
        cb->setChecked(false);
    }
    for (QCheckBox *cb : recordingCheckBoxes) {
        cb->setChecked(false);
    }
}

void SurgeryRecordingPage::showToast(const QString &message, int durationMs) {
    int toastFontSize = percentHeight(2.5, 16, 28);
    int toastPadding = percentHeight(1.5, 10, 20);
    int toastRadius = percentHeight(2, 15, 25);

    QLabel *toast = new QLabel(message, this);
    toast->setStyleSheet(
        QString("background-color: green; color: white; "
                "padding: %1px %2px; border-radius: %3px; font-size: %4px;")
        .arg(toastPadding).arg(toastPadding + 10).arg(toastRadius).arg(toastFontSize)
    );
    toast->setAttribute(Q*******ransparentForMouseEvents);
    toast->setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip);
    toast->adjustSize();

    int x = (width() - toast->width()) / 2;
    int y = height() - toast->height() - percentHeight(5, 40, 80);
    toast->move(x, y);
    toast->show();

    QTimer::singleShot(durationMs, toast, &QLabel::deleteLater);
}