#ifndef PDFVIEWERPAGE_HPP
#define PDFVIEWERPAGE_HPP

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>

class PdfViewerPage : public QWidget
{
    Q_OBJECT
public:
    explicit PdfViewerPage(QWidget *parent = nullptr);
    void loadPdf(const QString &pdfPath);

private slots:
    void downloadReport();
    void zoomIn();
    void zoomOut();
    void resetZoom();

private:
    void renderPdf();
    QVBoxLayout *mainLayout;
    QScrollArea *scrollArea;
    QWidget *contentWidget;
    QPushButton *downloadButton;
    QPushButton *zoomInButton;
    QPushButton *zoomOutButton;
    QPushButton *resetZoomButton;
    QLabel *zoomLabel;

        
    QString currentPdfPath;
    int zoomLevel;          // Percentage: 100 = 100%
    static const int MIN_ZOOM = 50;
    static const int MAX_ZOOM = 300;
    static const int ZOOM_STEP = 25;
};

#endif // PDFVIEWERPAGE_HPP
