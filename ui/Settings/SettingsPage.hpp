#ifndef SETTINGSPAGE_HPP
#define SETTINGSPAGE_HPP

#include <QWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QCheckBox>

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

signals:
    void logoChanged(const QString &newLogoPath);
    void videoInputChanged(const QString &newInput);
    void videoLabelVisibilityChanged(bool visible);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void saveSettingsToDatabase();
    void loadSettingsFromDatabase();
    void togglePasswordVisibility();

private:
    void setupUI();
    void applyStyles();
    void updateScaling();
    
    int percentWidth(int percent, int minVal = 0, int maxVal = 9999);
    int percentHeight(int percent, int minVal = 0, int maxVal = 9999);

    QVBoxLayout* mainLayout;
    QScrollArea* scrollArea;
    QWidget* scrollContent;
    QVBoxLayout* scrollLayout;
    
    QHBoxLayout* twoColLayout;
    QVBoxLayout* leftCol;
    QVBoxLayout* rightCol;

    // Left column
    QLabel* logoLabel;
    QLineEdit* logoPathEdit;
    QPushButton* uploadLogoBtn;
    
    QLabel* hospitalNameLabel;
    QLineEdit* hospitalNameEdit;
    
    QLabel* softwareNameLabel;
    QLineEdit* softwareNameEdit;
    
    QLabel* hospitalEmailLabel;
    QLineEdit* hospitalEmailEdit;
    
    QLabel* hospitalAddressLabel;
    QTextEdit* hospitalAddressEdit;
    
    QLabel* stateLabel;
    QLineEdit* stateEdit;

    // Right column
    QLabel* districtLabel;
    QLineEdit* districtEdit;
    
    QLabel* pinLabel;
    QLineEdit* pinEdit;
    
    QLabel* aboutHospitalLabel;
    QTextEdit* aboutHospitalEdit;
    
    QLabel* hospitalPhoneLabel;
    QLineEdit* hospitalPhoneEdit;
    
    QLabel* videoInputLabel;
    QComboBox* videoInputCombo;

    QCheckBox* showLabelCheckbox;
    QLabel* showLabelLabel;

    // Hidden fields
    QLineEdit* watermarkPathEdit;
    QPushButton* uploadWatermarkBtn;
    QLineEdit* storagePathEdit;
    QLineEdit* rtspLinkEdit;
    QPushButton* togglePasswordBtn;

    QPushButton* saveButton;

    int m_lastWidth = 0;
    int m_lastHeight = 0;
};

#endif // SETTINGSPAGE_HPP