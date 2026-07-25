#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QTimer>
#include "serialmonitor.h"

class QComboBox;
class QCheckBox;
class QTextEdit;
class QGroupBox;
class QProgressBar;
class QLineEdit;
class QTabWidget;
class QLabel;
class QPushButton;
class QSpinBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onGenerateKey();
    void onSelectKey();
    void onSelectBootloader();
    void onSelectPartition();
    void onSelectFirmware();
    void onFlash();
    void onEraseFlash();
    void onClearAll();
    void onBurnSecurityFuses();
    void onReadFuseStatus();
    void onRefreshPorts();

    void onPortDetected(const SimplePortInfo &port);
    void onPortRemoved(const QString &portName);

    void onProcessOutput();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void updateProgress();

private:
    void setupUI();
    void createConnections();
    void appendLog(const QString &message, const QString &color = QString());
    void runModuleCommand(const QString &module, const QStringList &args);
    QString getPythonPath();
    bool checkPythonAvailable();
    bool checkModuleAvailable(const QString &module);
    void updateStatus(const QString &status);
    void enableControls(bool enable);
    void loadSettings();
    void saveSettings();
    void updateFlashButton();
    QString formatOffset(quint32 offset);
    void setupAutoSave();
    void populateBaudRates();
    void checkEncryptionStatus();
    void updateEncryptionStatusLabel(bool configured, const QString &details = QString());
    void flashWithEncryption();
    void flashWithoutEncryption();
    void performEncryptionSetup();
    void handleEncryptionStepComplete();
    void parseChipInfo(const QString &output);
    void parseFlashSize(const QString &output);
    void parseEncryptionStatus(const QString &output);
    void parseFuseStatus(const QString &output);
    void executeNextCommand();
    void checkAndHandleKeyBurned();
    void encryptFirmwareWithKey();
    QString getEncryptedFirmwarePath();
    void cleanupEncryptedFile();
    void burnNextFuse();
    void flashEncryptedFirmwareDirectly();
    QString getFlashMode();
    QString getFlashFreq();
    QString getFlashSize();
    void resetESP32();
    void checkAndInstallDependencies();
    void installEspressifTools();

    // Tabs
    QTabWidget *mainTabs;
    QWidget *flashTab;
    QWidget *fuseTab;
    QWidget *aboutTab;

    // Port
    QComboBox *portComboBox;
    QLabel *portStatusLabel;
    QString currentPort;

    // Chip info
    QGroupBox *chipInfoGroup;
    QLabel *chipModelLabel;
    QLabel *chipRevisionLabel;
    QLabel *chipCoresLabel;
    QLabel *chipFeaturesLabel;
    QLabel *chipMacLabel;
    QLabel *flashSizeLabel;
    QLabel *encryptionStatusLabel;

    // Key management
    QPushButton *generateKeyBtn;
    QPushButton *selectKeyBtn;
    QLineEdit *keyFileEdit;
    QString selectedKeyPath;
    QString pendingKeyPath;

    // Bootloader
    QPushButton *selectBootloaderBtn;
    QLineEdit *bootloaderFileEdit;
    QSpinBox *bootloaderOffsetSpin;
    QString selectedBootloaderPath;

    // Partition Table
    QPushButton *selectPartitionBtn;
    QLineEdit *partitionFileEdit;
    QSpinBox *partitionOffsetSpin;
    QString selectedPartitionPath;

    // Firmware
    QPushButton *selectFirmwareBtn;
    QLineEdit *firmwareFileEdit;
    QSpinBox *firmwareOffsetSpin;
    QString selectedFirmwarePath;

    // Baud Rate
    QComboBox *baudRateComboBox;

    // Options
    QCheckBox *encryptFlashCheckBox;
    QCheckBox *compressCheckBox;

    // Actions
    QPushButton *flashBtn;
    QPushButton *eraseFlashBtn;
    QPushButton *clearAllBtn;

    // Progress / log
    QProgressBar *progressBar;
    QTextEdit *logTextEdit;

    // Status bar
    QLabel *statusLabel;

    // Fuse tab controls
    QGroupBox *fuseOptionsGroup;
    QCheckBox *uartDownloadDisCheckBox;
    QCheckBox *jtagDisableCheckBox;
    QCheckBox *disableDlEncryptCheckBox;
    QCheckBox *disableDlDecryptCheckBox;
    QCheckBox *disableCacheCheckBox;
    QCheckBox *consoleDebugDisableCheckBox;
    QPushButton *burnSecurityFusesBtn;
    QPushButton *readFuseStatusBtn;

    // Fuse status labels
    QLabel *uartFuseStatus;
    QLabel *jtagFuseStatus;
    QLabel *dlEncryptStatus;
    QLabel *dlDecryptStatus;
    QLabel *cacheStatus;
    QLabel *consoleDebugStatus;
    QLabel *flashCryptCntStatus;
    QLabel *flashCryptConfigStatus;

    QStringList fuseBurnQueue;
    int fuseBurnIndex;

    // State
    bool isFlashing;
    bool isSettingLoaded;
    bool isEncryptionConfigured;
    bool encryptionSetupInProgress;
    bool isCheckingEncryption;
    bool isFirstEncryptionCheck;
    bool chipInfoReceived;
    bool flashSizeReceived;
    bool commandQueueRunning;
    bool keyBurnSkipped;
    bool isBurningFuses;
    bool dependenciesChecked;
    int flashRetryCount;
    const int MAX_FLASH_RETRIES = 3;
    QStringList pendingEncryptionSteps;
    int encryptionStepIndex;
    int commandQueueIndex;
    QStringList commandQueue;
    QString encryptedFirmwarePath;

    QProcess *esptoolProcess;
    SerialMonitor *serialMonitor;
    QString processOutputBuffer;
    QString pendingModule;
    QStringList pendingArgs;

    // Settings
    QSettings *settings;
    QTimer *encryptionCheckTimer;
};