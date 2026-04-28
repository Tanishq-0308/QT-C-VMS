#include "PdfViewerPage.hpp"
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QDebug>
#include <QPushButton>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <poppler-qt5.h>

PdfViewerPage::PdfViewerPage(QWidget *parent) : QWidget(parent), zoomLevel(100)
{
    mainLayout = new QVBoxLayout(this);

    // Top toolbar with download and zoom buttons
    QHBoxLayout *toolbarLayout = new QHBoxLayout;
    
    // Download button
    downloadButton = new QPushButton("Download", this);
    downloadButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #0055aa;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "   font-size: 18px;"
        "   padding: 10px 20px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #0066cc;"
        "}"
    );
    
    // Zoom out button
    zoomOutButton = new QPushButton("-", this);
    zoomOutButton->setFixedSize(40, 40);
    zoomOutButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #444;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "   font-size: 24px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #666;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #999;"
        "}"
    );
    
    // Zoom label
    zoomLabel = new QLabel("100%", this);
    zoomLabel->setFixedWidth(60);
    zoomLabel->setAlignment(Qt::AlignCenter);
    zoomLabel->setStyleSheet(
        "font-size: 16px;"
        "font-weight: bold;"
        "color: #333;"
    );
    
    // Zoom in button
    zoomInButton = new QPushButton("+", this);
    zoomInButton->setFixedSize(40, 40);
    zoomInButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #444;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "   font-size: 24px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #666;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #999;"
        "}"
    );
    
    // Reset zoom button
    resetZoomButton = new QPushButton("Reset", this);
    resetZoomButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #666;"
        "   color: white;"
        "   font-weight: bold;"
        "   border-radius: 5px;"
        "   font-size: 14px;"
        "   padding: 8px 15px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #888;"
        "}"
    );

    // Add widgets to toolbar
    toolbarLayout->addWidget(downloadButton);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(zoomOutButton);
    toolbarLayout->addWidget(zoomLabel);
    toolbarLayout->addWidget(zoomInButton);
    toolbarLayout->addSpacing(10);
    toolbarLayout->addWidget(resetZoomButton);
    toolbarLayout->addStretch();

    // Scroll area for PDF content
    scrollArea = new QScrollArea(this);
    contentWidget = new QWidget(scrollArea);
    contentWidget->setLayout(new QVBoxLayout);
    scrollArea->setWidget(contentWidget);
    scrollArea->setWidgetResizable(true);

    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(scrollArea);

    setLayout(mainLayout);

    // Connect signals
    connect(downloadButton, &QPushButton::clicked, this, &PdfViewerPage::downloadReport);
    connect(zoomInButton, &QPushButton::clicked, this, &PdfViewerPage::zoomIn);
    connect(zoomOutButton, &QPushButton::clicked, this, &PdfViewerPage::zoomOut);
    connect(resetZoomButton, &QPushButton::clicked, this, &PdfViewerPage::resetZoom);
}

void PdfViewerPage::zoomIn()
{
    if (zoomLevel < MAX_ZOOM) {
        zoomLevel += ZOOM_STEP;
        zoomLabel->setText(QString("%1%").arg(zoomLevel));
        renderPdf();
    }
    
    // Update button states
    zoomInButton->setEnabled(zoomLevel < MAX_ZOOM);
    zoomOutButton->setEnabled(zoomLevel > MIN_ZOOM);
}

void PdfViewerPage::zoomOut()
{
    if (zoomLevel > MIN_ZOOM) {
        zoomLevel -= ZOOM_STEP;
        zoomLabel->setText(QString("%1%").arg(zoomLevel));
        renderPdf();
    }
    
    // Update button states
    zoomInButton->setEnabled(zoomLevel < MAX_ZOOM);
    zoomOutButton->setEnabled(zoomLevel > MIN_ZOOM);
}

void PdfViewerPage::resetZoom()
{
    zoomLevel = 100;
    zoomLabel->setText("100%");
    renderPdf();
    
    // Update button states
    zoomInButton->setEnabled(true);
    zoomOutButton->setEnabled(true);
}

void PdfViewerPage::loadPdf(const QString &pdfPath)
{
    currentPdfPath = pdfPath;
    zoomLevel = 100;
    zoomLabel->setText("100%");
    zoomInButton->setEnabled(true);
    zoomOutButton->setEnabled(true);
    renderPdf();
}

void PdfViewerPage::renderPdf()
{
    if (currentPdfPath.isEmpty()) return;
    
    // Clear previous content
    QLayout *layout = contentWidget->layout();
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    Poppler::Document *doc = Poppler::Document::load(currentPdfPath);
    if (!doc) {
        qWarning() << "Failed to load PDF:" << currentPdfPath;
        return;
    }

    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);

    // Base DPI is 150, scale by zoom level
    const int baseDpi = 150;
    const int dpi = baseDpi * zoomLevel / 100;

    for (int i = 0; i < doc->numPages(); ++i) {
        Poppler::Page *page = doc->page(i);
        if (!page) continue;

        QImage image = page->renderToImage(dpi, dpi);
        delete page;

        if (image.isNull()) continue;

        QLabel *pageLabel = new QLabel;
        pageLabel->setPixmap(QPixmap::fromImage(image));
        pageLabel->setAlignment(Qt::AlignCenter);

        pageLabel->setStyleSheet(
            "border: 2px solid #444;"
            "margin: 10px;"
            "background-color: white;"
        );

        QWidget *container = new QWidget;
        QVBoxLayout *containerLayout = new QVBoxLayout(container);
        containerLayout->addWidget(pageLabel, 0, Qt::AlignCenter);
        containerLayout->setContentsMargins(0, 10, 0, 10);

        layout->addWidget(container);
    }

    delete doc;
}

void PdfViewerPage::downloadReport()
{
    QString sourcePath = "/home/brainwave/medical_qt_app/flask_zoom_api/reports/report.pdf";

    if (!QFile::exists(sourcePath)) {
        QMessageBox::information(this,
                                 "Report Not Found",
                                 "The report has not been generated yet.\nPlease generate the PDF report before downloading.");
        qWarning() << "Report file not found at:" << sourcePath;
        return;
    }

    // Detect USB device under /media/brainwave/
    QString basePath = "/media/brainwave";
    QDir mediaDir(basePath);
    QString destDir;

    if (mediaDir.exists()) {
        QStringList deviceDirs = mediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        if (!deviceDirs.isEmpty()) {
            QString usbMountPath = basePath + "/" + deviceDirs.first();
            destDir = usbMountPath + "/Reports";
        }
    }

    if (destDir.isEmpty()) {
        QMessageBox::warning(this,
                             "No USB Device Found",
                             "Please connect a USB device before attempting to download the report.");
        qWarning() << "No USB mount found under:" << basePath;
        return;
    }

    QDir().mkpath(destDir);

    QFileInfo fileInfo(sourcePath);
    QString destPath = destDir + "/" + fileInfo.fileName();

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    if (QFile::copy(sourcePath, destPath)) {
        QMessageBox::information(this,
                                 "Download Successful",
                                 QString("Report successfully downloaded to:\n%1").arg(destPath));
        qDebug() << "Report copied to:" << destPath;
    } else {
        QMessageBox::warning(this,
                             "Download Error",
                             "Failed to download the report to the USB device.");
        qWarning() << "Failed to copy report from:" << sourcePath << " to " << destPath;
    }
}