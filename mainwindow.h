#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QVector>
#include <QPair>
#include <QTextCursor>
#include <QTextCharFormat>

#include "serialmonitor.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QTextEdit;
class QGroupBox;
class QProgressBar;
class QLineEdit;
class QTabWidget;
class QSpinBox;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
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
    void onRefreshPorts();
    void onReadFuseStatus();
    void onBurnSecurityFuses();
    void onPortDetected(const SimplePortInfo &port);
    void onPortRemoved(const QString &portName);
    void onProcessOutput();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void updateProgress();
    void checkEncryptionStatus();
    void updateFlashButton();
    void onUpdateFlashButtonClicked();
    void saveSettings();
    void loadSettings();
    void checkAndInstallDependencies();
    void installEspressifTools();

private:
    void setupUI();
    void createConnections();
    void setupAutoSave();
    void populateBaudRates();
    void updateStatus(const QString &status);
    void enableControls(bool enable);
    void appendLog(const QString &message, const QString &color = "");
    QString sanitizeEspOutput(const QString &raw);
    void runModuleCommand(const QString &module, const QStringList &args);
    QString getPythonPath();
    bool checkPythonAvailable();
    bool checkModuleAvailable(const QString &module);

    void parseChipInfo(const QString &output);
    void parseFlashSize(const QString &output);
    void parseEncryptionStatus(const QString &output);
    void parseFuseStatus(const QString &output);

    void executeNextCommand();
    void performEncryptionSetup();
    void handleEncryptionStepComplete();
    void encryptFirmwareWithKey();
    void encryptNextFile();
    void flashWithEncryption();
    void flashEncryptedFirmwareDirectly();
    void flashWithoutEncryption();
    void checkAndHandleKeyBurned();
    void resetESP32();
    void burnSecurityFuses();
    void burnNextFuse();

    QString formatOffset(quint32 offset);
    QString getEncryptedFirmwarePath();
    QString getEncryptedFilePath(const QString &originalPath, const QString &suffix);
    QString ensureNoSpacePath(const QString &originalPath);
    void cleanupEncryptedFile();
    void cleanupTempDirectory();
    void updateEncryptionStatusLabel(bool configured, const QString &details = "");

    QString getFlashMode();
    QString getFlashFreq();
    QString getFlashSize();

    // UI - Flash tab
    QTabWidget *mainTabs;
    QWidget *flashTab;
    QWidget *fuseTab;
    QWidget *aboutTab;

    QComboBox *portComboBox;
    QLabel *portStatusLabel;
    QLabel *statusLabel;

    QGroupBox *chipInfoGroup;
    QLabel *chipModelLabel;
    QLabel *chipRevisionLabel;
    QLabel *chipCoresLabel;
    QLabel *chipFeaturesLabel;
    QLabel *chipMacLabel;
    QLabel *flashSizeLabel;
    QLabel *encryptionStatusLabel;

    QPushButton *generateKeyBtn;
    QPushButton *selectKeyBtn;
    QLineEdit *keyFileEdit;

    QPushButton *selectBootloaderBtn;
    QPushButton *selectPartitionBtn;
    QPushButton *selectFirmwareBtn;
    QLineEdit *bootloaderFileEdit;
    QLineEdit *partitionFileEdit;
    QLineEdit *firmwareFileEdit;

    QSpinBox *bootloaderOffsetSpin;
    QSpinBox *partitionOffsetSpin;
    QSpinBox *firmwareOffsetSpin;

    QComboBox *baudRateComboBox;
    QCheckBox *encryptFlashCheckBox;
    QCheckBox *compressCheckBox;

    QPushButton *flashBtn;
    QPushButton *eraseFlashBtn;
    QPushButton *clearAllBtn;

    QProgressBar *progressBar;
    QTextEdit *logTextEdit;

    // UI - Fuse tab
    QLabel *uartFuseStatus;
    QLabel *jtagFuseStatus;
    QLabel *dlEncryptStatus;
    QLabel *dlDecryptStatus;
    QLabel *cacheStatus;
    QLabel *consoleDebugStatus;
    QLabel *flashCryptCntStatus;
    QLabel *flashCryptConfigStatus;

    QPushButton *readFuseStatusBtn;
    QGroupBox *fuseOptionsGroup;
    QPushButton *burnSecurityFusesBtn;

    QCheckBox *uartDownloadDisCheckBox;
    QCheckBox *jtagDisableCheckBox;
    QCheckBox *disableDlEncryptCheckBox;
    QCheckBox *disableDlDecryptCheckBox;
    QCheckBox *disableCacheCheckBox;
    QCheckBox *consoleDebugDisableCheckBox;

    // State
    bool isFlashing;
    bool isSettingLoaded;
    bool isEncryptionConfigured;
    bool encryptionSetupInProgress;
    bool isCheckingEncryption;
    bool isFirstEncryptionCheck;
    bool isEncryptionDetectionParsed;
    bool chipInfoReceived;
    bool flashSizeReceived;
    bool commandQueueRunning;
    bool keyBurnSkipped;
    bool isBurningFuses;
    bool dependenciesChecked;
    bool isFlashMode;
    bool flashingEncryptedImages;   // dedicated flag for encrypted flash detection
    bool handlingKeyBurned;         // guard against double-calling checkAndHandleKeyBurned()
    bool pendingEncryptionSuccessDialog = false;

    int fuseBurnIndex;
    int flashRetryCount;
    int encryptionStepIndex;
    int commandQueueIndex;

    static const int MAX_FLASH_RETRIES = 3;

    QProcess *esptoolProcess;
    SerialMonitor *serialMonitor;
    QSettings *settings;
    QTimer *encryptionCheckTimer;

    QString currentPort;

    QString selectedKeyPath;
    QString selectedBootloaderPath;
    QString selectedPartitionPath;
    QString selectedFirmwarePath;

    QString pendingModule;
    QStringList pendingArgs;
    QString processOutputBuffer;

    QString pendingKeyPath;

    QString encryptedFirmwarePath;
    QString encryptedBootloaderPath;
    QString encryptedPartitionPath;

    QString pendingEncryptedSourcePath;
    QString pendingEncryptedOutputPath;

    QVector<QPair<QString, quint32>> encryptionFileQueue;
    int encryptionFileIndex = 0;

    QStringList commandQueue;
    QStringList pendingEncryptionSteps;
    QStringList fuseBurnQueue;
};

#endif // MAINWINDOW_H