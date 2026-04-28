#ifndef SURGERYRECORDINGPAGE_HPP
#define SURGERYRECORDINGPAGE_HPP

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QGridLayout>
#include <QFrame>
#include <QSet>
#include <QList>
#include <QCheckBox>
#include <QResizeEvent>
#include <QShowEvent>
#include "../ClickableLabel/ClickableLabel.hpp"

// Structure to hold card widget references for scaling
struct CardWidgets {
    QWidget* card;
    ClickableLabel* thumbnail;
    QLabel* fileLabel;
    QCheckBox* checkBox;
    QPushButton* deleteBtn;  // NEW: Delete button for each card
    QString filePath;
    int fileId;              // NEW: Database ID for deletion
    bool isVideo;
};

class SurgeryRecordingPage : public QWidget
{
    Q_OBJECT

public:
    explicit SurgeryRecordingPage(const QString& patientId, int surgeryId, QWidget *parent = nullptr);
    void refreshRecordings();
    void showToast(const QString& message, int durationMs = 2000);

signals:
    void goBackRequested();
    void goToRecordingPage(const QString& patientId, int surgeryId);
    void requestPageChange(QWidget* newPage);
    void openPdfReport(const QString &pdfPath);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void handleThumbnailClick(const QString& filePath, int videoId);
    void downloadSelectedFiles();
    void deleteSelectedFiles();                                    // NEW
    void deleteSingleFile(int fileId, const QString& filePath, bool isVideo);  // NEW

private:
    void setupUI();
    void applyStyles();
    void updateScaling();

    // Responsive helpers
    int percentWidth(int percent, int minVal = 0, int maxVal = 9999);
    int percentHeight(int percent, int minVal = 0, int maxVal = 9999);
    int scaled(int base4kValue, int minVal = 0, int maxVal = 9999);

    QWidget* createSurgeryInfoSection();
    QWidget* createRecordingSection(const QString& title, const QString& tableName);
    void rebuildGridLayout(QGridLayout* grid, const QList<CardWidgets>& cards);
    void generateReport();
    void loadSurgeryDetails();
    
    // NEW: Delete helpers
    bool deleteFromDatabase(int fileId, bool isVideo);
    bool deleteFileFromStorage(const QString& filePath);

    // Main layout
    QVBoxLayout* mainLayout;

    // Top section widgets
    QPushButton* goBackBtn;
    QLabel* titleLabel;
    QLabel* recTitle;

    // Surgery info labels
    QLabel* surgeonNameLabel;
    QLabel* surgeryTypeLabel;
    QLabel* bodyPartLabel;
    QLabel* dateLabel;
    QLabel* additionalSurgeonLabel;
    QLabel* anesthesiologistLabel;
    QLabel* surgicalHistoryLabel;
    QLabel* theatreNumberLabel;

    // Action buttons
    QPushButton* editBtn;
    QPushButton* reportBtn;
    QPushButton* recordBtn;
    QPushButton* downloadBtn;
    QPushButton* deleteBtn;  // NEW: Delete selected button

    // Section titles
    QLabel* videoSectionTitle;
    QLabel* snapshotSectionTitle;

    // Grids
    QGridLayout* videoGrid;
    QGridLayout* snapshotGrid;

    // Sections
    QWidget* videoSection;
    QWidget* snapshotSection;

    // Store card references for responsive updates
    QList<CardWidgets> videoCards;
    QList<CardWidgets> snapshotCards;

    // Selection tracking
    QSet<QString> selectedSnapshots;
    QSet<QString> selectedRecordings;
    QList<QCheckBox*> snapshotCheckBoxes;
    QList<QCheckBox*> recordingCheckBoxes;
    
    // NEW: Track selected IDs for deletion
    QMap<QString, int> snapshotPathToId;
    QMap<QString, int> recordingPathToId;

    // IDs
    QString m_patientId;
    int m_surgeryId;

    // Track last size to avoid unnecessary updates
    int m_lastWidth = 0;
    int m_lastHeight = 0;
    bool m_stylesApplied = false;
};

#endif // SURGERYRECORDINGPAGE_HPP