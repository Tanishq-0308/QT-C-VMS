#pragma once

#include <QLabel>

class ClickableLabel : public QLabel {
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget* parent = nullptr);
    void setFilePath(const QString& path);
    QString filePath() const;

signals:
    void clicked(const QString& filePath);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString m_filePath;
};
