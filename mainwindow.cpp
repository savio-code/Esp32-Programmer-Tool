#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QGroupBox>
#include <QProgressBar>
#include <QLineEdit>
#include <QTabWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QFontDatabase>
#include <QScrollBar>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStatusBar>
#include <QSpinBox>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , isFlashing(false)
    , isSettingLoaded(false)
    , isEncryptionConfigured(false)
    , encryptionSetupInProgress(false)
    , isCheckingEncryption(false)
    , isFirstEncryptionCheck(true)
    , chipInfoReceived(false)
    , flashSizeReceived(false)
    , commandQueueRunning(false)
    , keyBurnSkipped(false)
    , isBurningFuses(false)
    , dependenciesChecked(false)  // Add this
    , fuseBurnIndex(0)
    , flashRetryCount(0)
    , encryptionStepIndex(0)
    , commandQueueIndex(0)
    , esptoolProcess(nullptr)
    , serialMonitor(nullptr)
    , settings(new QSettings("ESP32Tools", "Flasher", this))
    , encryptionCheckTimer(nullptr)
{
    setupUI();
    createConnections();
    setupAutoSave();
    loadSettings();

    encryptionCheckTimer = new QTimer(this);
    encryptionCheckTimer->setSingleShot(true);
    connect(encryptionCheckTimer, &QTimer::timeout, this, &MainWindow::checkEncryptionStatus);

    serialMonitor = new SerialMonitor(this);
    connect(serialMonitor, &SerialMonitor::portDetected,
            this, &MainWindow::onPortDetected);
    connect(serialMonitor, &SerialMonitor::portRemoved,
            this, &MainWindow::onPortRemoved);
    serialMonitor->start();

    updateStatus("Ready");
    appendLog("Application started. Waiting for ESP32...", "gray");
    updateFlashButton();

    // Check dependencies after UI is ready
    QTimer::singleShot(1000, this, &MainWindow::checkAndInstallDependencies);
}

MainWindow::~MainWindow()
{
    saveSettings();
    cleanupEncryptedFile();

    if (encryptionCheckTimer) {
        encryptionCheckTimer->stop();
        delete encryptionCheckTimer;
        encryptionCheckTimer = nullptr;
    }

    if (serialMonitor) {
        serialMonitor->stop();
        serialMonitor->wait();
        delete serialMonitor;
        serialMonitor = nullptr;
    }

    if (esptoolProcess) {
        esptoolProcess->disconnect();
        if (esptoolProcess->state() == QProcess::Running) {
            esptoolProcess->terminate();
            esptoolProcess->waitForFinished(2000);
        }
        delete esptoolProcess;
        esptoolProcess = nullptr;
    }
}


void MainWindow::setupUI()
{
    setWindowTitle("ESP32 Flasher & Encryption Manager");
    setMinimumSize(920, 750);
    resize(950, 780);

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    mainTabs = new QTabWidget(this);
    mainLayout->addWidget(mainTabs);

    // ==================== Flash Tab ====================
    flashTab = new QWidget();
    mainTabs->addTab(flashTab, "Flash Firmware");

    QVBoxLayout *flashLayout = new QVBoxLayout(flashTab);
    flashLayout->setSpacing(4);
    flashLayout->setContentsMargins(6, 6, 6, 6);

    // ========== Port Selection with Refresh Button ==========
    QHBoxLayout *portLayout = new QHBoxLayout();
    portLayout->setSpacing(5);

    QLabel *portLabel = new QLabel("Port:");
    portLabel->setFixedWidth(35);
    portLayout->addWidget(portLabel);

    portComboBox = new QComboBox();
    portComboBox->setMinimumWidth(150);
    portLayout->addWidget(portComboBox);

    statusLabel = new QLabel("Ready");
    statusBar()->addWidget(statusLabel);

    // ========== Add Check Dependencies Button ==========
    QPushButton *checkDepsBtn = new QPushButton("🔍 Check Dependencies");
    checkDepsBtn->setFixedWidth(150);
    checkDepsBtn->setStyleSheet("QPushButton { background-color: #17a2b8; font-weight: bold; font-size: 10px; padding: 2px 8px; }");
    connect(checkDepsBtn, &QPushButton::clicked, this, &MainWindow::checkAndInstallDependencies);
    statusBar()->addPermanentWidget(checkDepsBtn);


    // Refresh Button
    QPushButton *refreshBtn = new QPushButton("🔄 Refresh");
    refreshBtn->setFixedWidth(90);
    refreshBtn->setToolTip("Refresh port list and chip information");
    refreshBtn->setStyleSheet("QPushButton { background-color: #28a745; font-weight: bold; }");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    portLayout->addWidget(refreshBtn);

    portLayout->addStretch();
    flashLayout->addLayout(portLayout);

    portStatusLabel = new QLabel("🔴 No ESP32 detected");
    portStatusLabel->setStyleSheet("font-weight: bold;");
    flashLayout->addWidget(portStatusLabel);

    // ========== Chip Info ==========
    chipInfoGroup = new QGroupBox("ESP32 Info");
    chipInfoGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 6px; padding-top: 8px; }");
    QGridLayout *chipInfoLayout = new QGridLayout(chipInfoGroup);
    chipInfoLayout->setSpacing(2);
    chipInfoLayout->setContentsMargins(6, 8, 6, 4);

    chipModelLabel = new QLabel("Model: --");
    chipRevisionLabel = new QLabel("Rev: --");
    chipCoresLabel = new QLabel("Cores: --");
    chipFeaturesLabel = new QLabel("Features: --");
    chipMacLabel = new QLabel("MAC: --");
    flashSizeLabel = new QLabel("Flash: --");
    encryptionStatusLabel = new QLabel("🔓 Encryption: Checking...");
    encryptionStatusLabel->setStyleSheet("color: #ffd43b; font-weight: bold;");

    chipInfoLayout->addWidget(chipModelLabel, 0, 0);
    chipInfoLayout->addWidget(chipRevisionLabel, 0, 1);
    chipInfoLayout->addWidget(chipCoresLabel, 0, 2);
    chipInfoLayout->addWidget(chipFeaturesLabel, 1, 0, 1, 2);
    chipInfoLayout->addWidget(chipMacLabel, 1, 2);
    chipInfoLayout->addWidget(flashSizeLabel, 2, 0);
    chipInfoLayout->addWidget(encryptionStatusLabel, 2, 1, 1, 2);

    flashLayout->addWidget(chipInfoGroup);

    // ========== Key Management ==========
    QGroupBox *keyGroup = new QGroupBox("Encryption Key");
    keyGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 6px; padding-top: 8px; }");
    QHBoxLayout *keyLayout = new QHBoxLayout(keyGroup);
    keyLayout->setSpacing(4);
    keyLayout->setContentsMargins(6, 8, 6, 4);

    generateKeyBtn = new QPushButton("Generate Key");
    generateKeyBtn->setFixedWidth(100);
    selectKeyBtn = new QPushButton("Select Key");
    selectKeyBtn->setFixedWidth(90);
    keyFileEdit = new QLineEdit();
    keyFileEdit->setReadOnly(true);
    keyFileEdit->setPlaceholderText("No key selected");
    keyFileEdit->setMaximumHeight(24);

    keyLayout->addWidget(generateKeyBtn);
    keyLayout->addWidget(selectKeyBtn);
    keyLayout->addWidget(keyFileEdit);

    flashLayout->addWidget(keyGroup);

    // ========== Flash Files Group ==========
    QGroupBox *filesGroup = new QGroupBox("Flash Files");
    filesGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 6px; padding-top: 8px; }");
    QGridLayout *filesLayout = new QGridLayout(filesGroup);
    filesLayout->setSpacing(3);
    filesLayout->setContentsMargins(6, 8, 6, 4);

    // Row 1: Bootloader
    selectBootloaderBtn = new QPushButton("Bootloader");
    selectBootloaderBtn->setFixedWidth(90);
    bootloaderFileEdit = new QLineEdit();
    bootloaderFileEdit->setReadOnly(true);
    bootloaderFileEdit->setPlaceholderText("No bootloader");
    bootloaderFileEdit->setMaximumHeight(24);
    bootloaderOffsetSpin = new QSpinBox();
    bootloaderOffsetSpin->setRange(0, 0xFFFFFF);
    bootloaderOffsetSpin->setValue(0x1000);
    bootloaderOffsetSpin->setPrefix("0x");
    bootloaderOffsetSpin->setDisplayIntegerBase(16);
    bootloaderOffsetSpin->setMinimumWidth(100);
    bootloaderOffsetSpin->setMaximumWidth(120);
    bootloaderOffsetSpin->setMaximumHeight(24);

    filesLayout->addWidget(selectBootloaderBtn, 0, 0);
    filesLayout->addWidget(bootloaderFileEdit, 0, 1);
    filesLayout->addWidget(new QLabel("Offset:"), 0, 2);
    filesLayout->addWidget(bootloaderOffsetSpin, 0, 3);

    // Row 2: Partition Table
    selectPartitionBtn = new QPushButton("Partition");
    selectPartitionBtn->setFixedWidth(90);
    partitionFileEdit = new QLineEdit();
    partitionFileEdit->setReadOnly(true);
    partitionFileEdit->setPlaceholderText("No partition");
    partitionFileEdit->setMaximumHeight(24);
    partitionOffsetSpin = new QSpinBox();
    partitionOffsetSpin->setRange(0, 0xFFFFFF);
    partitionOffsetSpin->setValue(0x8000);
    partitionOffsetSpin->setPrefix("0x");
    partitionOffsetSpin->setDisplayIntegerBase(16);
    partitionOffsetSpin->setMinimumWidth(100);
    partitionOffsetSpin->setMaximumWidth(120);
    partitionOffsetSpin->setMaximumHeight(24);

    filesLayout->addWidget(selectPartitionBtn, 1, 0);
    filesLayout->addWidget(partitionFileEdit, 1, 1);
    filesLayout->addWidget(new QLabel("Offset:"), 1, 2);
    filesLayout->addWidget(partitionOffsetSpin, 1, 3);

    // Row 3: Firmware
    selectFirmwareBtn = new QPushButton("Firmware");
    selectFirmwareBtn->setFixedWidth(90);
    firmwareFileEdit = new QLineEdit();
    firmwareFileEdit->setReadOnly(true);
    firmwareFileEdit->setPlaceholderText("No firmware");
    firmwareFileEdit->setMaximumHeight(24);
    firmwareOffsetSpin = new QSpinBox();
    firmwareOffsetSpin->setRange(0, 0xFFFFFF);
    firmwareOffsetSpin->setValue(0x10000);
    firmwareOffsetSpin->setPrefix("0x");
    firmwareOffsetSpin->setDisplayIntegerBase(16);
    firmwareOffsetSpin->setMinimumWidth(100);
    firmwareOffsetSpin->setMaximumWidth(120);
    firmwareOffsetSpin->setMaximumHeight(24);

    filesLayout->addWidget(selectFirmwareBtn, 2, 0);
    filesLayout->addWidget(firmwareFileEdit, 2, 1);
    filesLayout->addWidget(new QLabel("Offset:"), 2, 2);
    filesLayout->addWidget(firmwareOffsetSpin, 2, 3);

    flashLayout->addWidget(filesGroup);

    // ========== Baud Rate & Options ==========
    QHBoxLayout *settingsLayout = new QHBoxLayout();
    settingsLayout->setSpacing(8);

    // Baud Rate
    QGroupBox *baudGroup = new QGroupBox("Baud Rate");
    baudGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 4px; padding-top: 6px; border: 1px solid #cccccc; border-radius: 4px; }");
    QHBoxLayout *baudLayout = new QHBoxLayout(baudGroup);
    baudLayout->setSpacing(3);
    baudLayout->setContentsMargins(4, 6, 4, 4);
    baudRateComboBox = new QComboBox();
    baudRateComboBox->setMinimumWidth(100);
    baudRateComboBox->setMaximumHeight(24);
    populateBaudRates();
    baudLayout->addWidget(baudRateComboBox);
    settingsLayout->addWidget(baudGroup);

    // Options
    QGroupBox *optionsGroup = new QGroupBox("Options");
    optionsGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 4px; padding-top: 6px; border: 1px solid #cccccc; border-radius: 4px; }");
    QHBoxLayout *optionsLayout = new QHBoxLayout(optionsGroup);
    optionsLayout->setSpacing(8);
    optionsLayout->setContentsMargins(6, 6, 6, 4);

    encryptFlashCheckBox = new QCheckBox("Encrypt Firmware");
    encryptFlashCheckBox->setToolTip("Encrypt the firmware before flashing");

    compressCheckBox = new QCheckBox("Compress");
    compressCheckBox->setToolTip("Compress data during flashing");
    compressCheckBox->setChecked(true);

    optionsLayout->addWidget(encryptFlashCheckBox);
    optionsLayout->addWidget(compressCheckBox);
    optionsLayout->addStretch();

    settingsLayout->addWidget(optionsGroup);
    flashLayout->addLayout(settingsLayout);

    // ========== Action Buttons ==========
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(6);

    flashBtn = new QPushButton("🔥 Flash Firmware");
    flashBtn->setStyleSheet("QPushButton { font-weight: bold; font-size: 13px; }");
    flashBtn->setMinimumHeight(34);

    eraseFlashBtn = new QPushButton("🗑️ Erase Flash");
    eraseFlashBtn->setStyleSheet("QPushButton { background-color: #dc3545; font-weight: bold; }");
    eraseFlashBtn->setMinimumHeight(34);
    eraseFlashBtn->setEnabled(false);

    clearAllBtn = new QPushButton("📂 Clear All");
    clearAllBtn->setStyleSheet("QPushButton { font-weight: bold; }");
    clearAllBtn->setMinimumHeight(34);
    connect(clearAllBtn, &QPushButton::clicked, this, &MainWindow::onClearAll);

    buttonLayout->addWidget(flashBtn);
    buttonLayout->addWidget(eraseFlashBtn);
    buttonLayout->addWidget(clearAllBtn);
    flashLayout->addLayout(buttonLayout);

    // ========== Progress ==========
    progressBar = new QProgressBar();
    progressBar->setVisible(false);
    progressBar->setMaximumHeight(18);
    flashLayout->addWidget(progressBar);

    // ========== Log Output ==========
    QGroupBox *logGroup = new QGroupBox("Log Output");
    logGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 4px; padding-top: 6px; }");
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    logLayout->setSpacing(2);
    logLayout->setContentsMargins(4, 6, 4, 4);

    // Log toolbar with clear button
    QHBoxLayout *logToolbar = new QHBoxLayout();
    logToolbar->setSpacing(4);

    QLabel *logLabel = new QLabel("Console Output");
    logLabel->setStyleSheet("font-weight: bold;");
    logToolbar->addWidget(logLabel);
    logToolbar->addStretch();

    QPushButton *clearLogBtn = new QPushButton("🗑️ Clear");
    clearLogBtn->setFixedWidth(70);
    clearLogBtn->setMinimumHeight(24);
    clearLogBtn->setStyleSheet("QPushButton { background-color: #6c757d; font-weight: bold; font-size: 10px; }");
    connect(clearLogBtn, &QPushButton::clicked, [this]() {
        logTextEdit->clear();
        appendLog("Console cleared", "gray");
    });
    logToolbar->addWidget(clearLogBtn);

    logLayout->addLayout(logToolbar);

    logTextEdit = new QTextEdit();
    logTextEdit->setReadOnly(true);
    logTextEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logTextEdit->setStyleSheet("QTextEdit { background-color: #1e1e1e; color: #d4d4d4; }");
    logTextEdit->setMaximumHeight(180);
    logLayout->addWidget(logTextEdit);

    flashLayout->addWidget(logGroup);

    // ==================== Fuse Tab ====================
    fuseTab = new QWidget();
    mainTabs->addTab(fuseTab, "Security Fuses");

    QVBoxLayout *fuseLayout = new QVBoxLayout(fuseTab);
    fuseLayout->setSpacing(6);
    fuseLayout->setContentsMargins(10, 10, 10, 10);

    // Fuse Status Section
    QGroupBox *statusGroup = new QGroupBox("Fuse Status");
    statusGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 10px; }");
    QGridLayout *statusLayout = new QGridLayout(statusGroup);
    statusLayout->setSpacing(4);
    statusLayout->setContentsMargins(10, 12, 10, 8);

    int row = 0;
    statusLayout->addWidget(new QLabel("UART Download:"), row, 0);
    uartFuseStatus = new QLabel("Unknown");
    uartFuseStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(uartFuseStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("JTAG:"), row, 0);
    jtagFuseStatus = new QLabel("Unknown");
    jtagFuseStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(jtagFuseStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("DL Encrypt:"), row, 0);
    dlEncryptStatus = new QLabel("Unknown");
    dlEncryptStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(dlEncryptStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("DL Decrypt:"), row, 0);
    dlDecryptStatus = new QLabel("Unknown");
    dlDecryptStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(dlDecryptStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("Cache:"), row, 0);
    cacheStatus = new QLabel("Unknown");
    cacheStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(cacheStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("Console Debug:"), row, 0);
    consoleDebugStatus = new QLabel("Unknown");
    consoleDebugStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(consoleDebugStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("FLASH_CRYPT_CNT:"), row, 0);
    flashCryptCntStatus = new QLabel("Unknown");
    flashCryptCntStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(flashCryptCntStatus, row, 1);
    row++;

    statusLayout->addWidget(new QLabel("FLASH_CRYPT_CONFIG:"), row, 0);
    flashCryptConfigStatus = new QLabel("Unknown");
    flashCryptConfigStatus->setStyleSheet("font-weight: bold;");
    statusLayout->addWidget(flashCryptConfigStatus, row, 1);
    row++;

    QHBoxLayout *statusBtnLayout = new QHBoxLayout();
    readFuseStatusBtn = new QPushButton("🔍 Read Fuse Status");
    readFuseStatusBtn->setMinimumHeight(30);
    connect(readFuseStatusBtn, &QPushButton::clicked, this, &MainWindow::onReadFuseStatus);
    statusBtnLayout->addWidget(readFuseStatusBtn);
    statusBtnLayout->addStretch();
    statusLayout->addLayout(statusBtnLayout, row, 0, 1, 2);

    fuseLayout->addWidget(statusGroup);

    // Fuse Burning Section
    fuseOptionsGroup = new QGroupBox("Burn Security Fuses (IRREVERSIBLE!)");
    fuseOptionsGroup->setStyleSheet("QGroupBox { font-weight: bold; color: #dc3545; margin-top: 8px; padding-top: 10px; }");
    QVBoxLayout *fuseOptionsLayout = new QVBoxLayout(fuseOptionsGroup);
    fuseOptionsLayout->setSpacing(4);
    fuseOptionsLayout->setContentsMargins(10, 12, 10, 8);

    QLabel *warningLabel = new QLabel("⚠️ WARNING: Burning any fuse is PERMANENT and CANNOT be undone!");
    warningLabel->setStyleSheet("color: #dc3545; font-weight: bold;");
    fuseOptionsLayout->addWidget(warningLabel);

    QGridLayout *fuseChecksLayout = new QGridLayout();
    fuseChecksLayout->setSpacing(4);
    fuseChecksLayout->setContentsMargins(0, 4, 0, 4);

    int frow = 0;
    uartDownloadDisCheckBox = new QCheckBox("Disable UART Download Mode");
    uartDownloadDisCheckBox->setToolTip("Disables UART download mode. ESP32 will not accept new firmware via serial.");
    fuseChecksLayout->addWidget(uartDownloadDisCheckBox, frow, 0);
    frow++;

    jtagDisableCheckBox = new QCheckBox("Disable JTAG");
    jtagDisableCheckBox->setToolTip("Disables JTAG debugging interface.");
    fuseChecksLayout->addWidget(jtagDisableCheckBox, frow, 0);
    frow++;

    disableDlEncryptCheckBox = new QCheckBox("Disable UART Flash Encryption");
    disableDlEncryptCheckBox->setToolTip("Disables flash encryption in UART bootloader.");
    fuseChecksLayout->addWidget(disableDlEncryptCheckBox, frow, 0);
    frow++;

    disableDlDecryptCheckBox = new QCheckBox("Disable UART Flash Decryption");
    disableDlDecryptCheckBox->setToolTip("Disables flash decryption in UART bootloader.");
    fuseChecksLayout->addWidget(disableDlDecryptCheckBox, frow, 0);
    frow++;

    disableCacheCheckBox = new QCheckBox("Disable Cache");
    disableCacheCheckBox->setToolTip("Disables cache memory.");
    fuseChecksLayout->addWidget(disableCacheCheckBox, frow, 0);
    frow++;

    consoleDebugDisableCheckBox = new QCheckBox("Disable Console Debug");
    consoleDebugDisableCheckBox->setToolTip("Disables ROM BASIC interpreter fallback.");
    fuseChecksLayout->addWidget(consoleDebugDisableCheckBox, frow, 0);
    frow++;

    fuseOptionsLayout->addLayout(fuseChecksLayout);

    QHBoxLayout *fuseBtnLayout = new QHBoxLayout();
    burnSecurityFusesBtn = new QPushButton("🔥 Burn Selected Fuses");
    burnSecurityFusesBtn->setStyleSheet("QPushButton { background-color: #dc3545; font-weight: bold; }");
    burnSecurityFusesBtn->setMinimumHeight(35);
    connect(burnSecurityFusesBtn, &QPushButton::clicked, this, &MainWindow::onBurnSecurityFuses);

    QPushButton *clearFuseChecksBtn = new QPushButton("Clear All");
    clearFuseChecksBtn->setStyleSheet("QPushButton { background-color: #6c757d; }");
    clearFuseChecksBtn->setMinimumHeight(35);
    connect(clearFuseChecksBtn, &QPushButton::clicked, [this]() {
        uartDownloadDisCheckBox->setChecked(false);
        jtagDisableCheckBox->setChecked(false);
        disableDlEncryptCheckBox->setChecked(false);
        disableDlDecryptCheckBox->setChecked(false);
        disableCacheCheckBox->setChecked(false);
        consoleDebugDisableCheckBox->setChecked(false);
        appendLog("All fuse selections cleared", "gray");
    });

    fuseBtnLayout->addWidget(burnSecurityFusesBtn);
    fuseBtnLayout->addWidget(clearFuseChecksBtn);
    fuseBtnLayout->addStretch();
    fuseOptionsLayout->addLayout(fuseBtnLayout);

    fuseLayout->addWidget(fuseOptionsGroup);

    // Fuse Info Section
    QGroupBox *infoGroup = new QGroupBox("Fuse Information");
    infoGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 10px; }");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setSpacing(4);
    infoLayout->setContentsMargins(10, 12, 10, 8);

    QTextEdit *infoText = new QTextEdit();
    infoText->setReadOnly(true);
    infoText->setHtml(R"(
        <p><b>Fuse Descriptions:</b></p>
        <p><b>Disable UART Download Mode:</b> Prevents flashing via UART. The chip will no longer accept new firmware over serial.</p>
        <p><b>Disable JTAG:</b> Disables the JTAG debugging interface, preventing external debugging.</p>
        <p><b>Disable UART Flash Encryption:</b> Disables encryption of flash writes in UART bootloader.</p>
        <p><b>Disable UART Flash Decryption:</b> Disables decryption of flash reads in UART bootloader.</p>
        <p><b>Disable Cache:</b> Disables cache memory, affecting performance.</p>
        <p><b>Disable Console Debug:</b> Disables ROM BASIC interpreter fallback.</p>
        <br>
        <p style='color: #dc3545;'><b>⚠️ ALL FUSE BURNS ARE PERMANENT AND IRREVERSIBLE!</b></p>
        <p style='color: #ffd43b;'>Once burned, these features cannot be re-enabled.</p>
    )");
    infoLayout->addWidget(infoText);

    fuseLayout->addWidget(infoGroup);

    // ==================== About Tab ====================
    aboutTab = new QWidget();
    mainTabs->addTab(aboutTab, "ℹ️ About");

    QVBoxLayout *aboutLayout = new QVBoxLayout(aboutTab);
    aboutLayout->setSpacing(12);
    aboutLayout->setContentsMargins(20, 20, 20, 20);

    // Header
    QLabel *titleLabel = new QLabel("ESP32 Flasher & Encryption Manager");
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #4a90d9;");
    titleLabel->setAlignment(Qt::AlignCenter);
    aboutLayout->addWidget(titleLabel);

    QLabel *versionLabel = new QLabel("Version 1.0");
    versionLabel->setStyleSheet("font-size: 14px; color: #666;");
    versionLabel->setAlignment(Qt::AlignCenter);
    aboutLayout->addWidget(versionLabel);

    // Separator
    QFrame *line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    aboutLayout->addWidget(line);

    // Description
    QTextEdit *descText = new QTextEdit();
    descText->setReadOnly(true);
    descText->setHtml(R"(
        <p style='font-size: 12px; line-height: 1.6;'>
            <b>ESP32 Flasher & Encryption Manager</b> is a comprehensive tool for managing
            ESP32 devices with flash encryption support.
        </p>

        <p style='font-size: 12px; line-height: 1.6;'>
            <b>Features:</b><br>
            • Flash firmware with or without encryption<br>
            • Manage encryption keys (generate, select, burn)<br>
            • Burn security fuses (UART, JTAG, etc.)<br>
            • Hardware encryption support for already-encrypted chips<br>
            • Automatic key selection after generation<br>
            • Real-time chip information and encryption status<br>
            • Multi-file flashing (bootloader, partition, firmware)<br>
            • Configurable baud rate and flash settings<br>
            • Security fuse management<br>
            • Auto-retry on failed operations
        </p>
        <br>
        <p style='font-size: 12px; line-height: 1.6;'>
            <b>Technologies:</b><br>
            • Built with Qt 6.11.0<br>
            • Uses esptool.py, espefuse.py, espsecure.py<br>
            • Windows native serial port detection
        </p>
    )");
    descText->setMaximumHeight(250);
    descText->setStyleSheet("QTextEdit { background-color: #f8f9fa; border: none; }");
    aboutLayout->addWidget(descText);

    // Author Information
    QGroupBox *authorGroup = new QGroupBox("Author Information");
    authorGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 10px; }");
    QVBoxLayout *authorLayout = new QVBoxLayout(authorGroup);
    authorLayout->setSpacing(6);
    authorLayout->setContentsMargins(12, 12, 12, 12);

    QLabel *authorName = new QLabel("👤 Saviour Emmanuel Ekiko");
    authorName->setStyleSheet("font-size: 14px; font-weight: bold;");
    authorLayout->addWidget(authorName);

    QLabel *authorRole = new QLabel("Embedded Systems Developer & Software Engineer");
    authorRole->setStyleSheet("font-size: 12px; color: #555;");
    authorLayout->addWidget(authorRole);

    QLabel *authorGithub = new QLabel("<a href='https://www.github.com/savio-code' style='color: #555; text-decoration: none;'>🐙 https://www.github.com/savio-code</a>");
    authorGithub->setOpenExternalLinks(true);
    authorGithub->setTextFormat(Qt::RichText);
    authorGithub->setStyleSheet("font-size: 12px;");
    authorGithub->setToolTip("Open GitHub profile in browser");
    authorLayout->addWidget(authorGithub);


    aboutLayout->addWidget(authorGroup);

    // License Information
    QGroupBox *licenseGroup = new QGroupBox("License");
    licenseGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 10px; }");
    QVBoxLayout *licenseLayout = new QVBoxLayout(licenseGroup);
    licenseLayout->setSpacing(6);
    licenseLayout->setContentsMargins(12, 12, 12, 12);

    QLabel *licenseText = new QLabel("This software is provided 'as is' without any warranty.\n"
                                     "Use at your own risk. The author assumes no liability for any\n"
                                     "damage or loss caused by the use of this software.");
    licenseText->setStyleSheet("font-size: 11px; color: #666;");
    licenseText->setWordWrap(true);
    licenseLayout->addWidget(licenseText);

    QLabel *licenseCopyright = new QLabel("© 2025 Saviour Ekiko. All rights reserved.");
    licenseCopyright->setStyleSheet("font-size: 11px; color: #888;");
    licenseCopyright->setAlignment(Qt::AlignCenter);
    licenseLayout->addWidget(licenseCopyright);

    aboutLayout->addWidget(licenseGroup);

    // Credits
    QGroupBox *creditsGroup = new QGroupBox("Credits");
    creditsGroup->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; padding-top: 10px; }");
    QVBoxLayout *creditsLayout = new QVBoxLayout(creditsGroup);
    creditsLayout->setSpacing(4);
    creditsLayout->setContentsMargins(12, 12, 12, 12);

    QLabel *creditsText = new QLabel(
        "• Espressif Systems for esptool.py, espefuse.py, and espsecure.py\n"
        "• Qt Framework for the GUI\n"
        "• PlatformIO community for testing and feedback"
        );
    creditsText->setStyleSheet("font-size: 11px; color: #555;");
    creditsText->setWordWrap(true);
    creditsLayout->addWidget(creditsText);

    aboutLayout->addWidget(creditsGroup);

    // Spacer to push everything up
    aboutLayout->addStretch();

    // ========== Status Bar ==========
    statusLabel = new QLabel("Ready");
    statusBar()->addWidget(statusLabel);

    // ========== Styles ==========
    setStyleSheet(R"(
        QGroupBox {
            font-weight: bold;
            border: 1px solid #cccccc;
            border-radius: 4px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px 0 4px;
        }
        QPushButton {
            background-color: #4a90d9;
            color: white;
            border: none;
            padding: 5px 12px;
            border-radius: 3px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #357abd;
        }
        QPushButton:pressed {
            background-color: #2a5f8f;
        }
        QPushButton:disabled {
            background-color: #808080;
        }
        QLineEdit {
            padding: 3px 6px;
            border: 1px solid #cccccc;
            border-radius: 3px;
        }
        QComboBox {
            padding: 3px 6px;
            border: 1px solid #cccccc;
            border-radius: 3px;
        }
        QCheckBox {
            padding: 2px;
        }
        QSpinBox {
            padding: 3px 6px;
            border: 1px solid #cccccc;
            border-radius: 3px;
            min-width: 100px;
        }
        QTabWidget::pane {
            border: 1px solid #cccccc;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #f0f0f0;
            padding: 6px 14px;
            margin-right: 2px;
            border-top-left-radius: 3px;
            border-top-right-radius: 3px;
        }
        QTabBar::tab:selected {
            background-color: #4a90d9;
            color: white;
        }
        QLabel {
            font-size: 11px;
        }
    )");
}

void MainWindow::checkAndInstallDependencies()
{
    appendLog("========================================", "gray");
    appendLog("🔍 Checking Python and Espressif tools...", "gray");
    appendLog("========================================", "gray");

    // Check Python
    QString python = getPythonPath();
    if (python.isEmpty()) {
        appendLog("❌ Python not found!", "red");
        appendLog("💡 Please install Python 3.8+ from:", "yellow");
        appendLog("   https://www.python.org/downloads/", "gray");
        appendLog("   Make sure to check 'Add Python to PATH' during installation.", "gray");
        QMessageBox::warning(this, "Python Not Found",
                             "Python is not installed or not in PATH.\n\n"
                             "Please install Python 3.8+ from:\n"
                             "https://www.python.org/downloads/\n\n"
                             "Make sure to check 'Add Python to PATH' during installation.");
        return;
    }
    appendLog("✅ Python found: " + python, "green");

    // Check esptool (which includes espsecure and espefuse)
    if (!checkModuleAvailable("esptool")) {
        appendLog("❌ esptool not found!", "red");
        appendLog("💡 Please install Espressif tools:", "yellow");
        appendLog("   pip install esptool", "gray");
        appendLog("   (This includes espsecure and espefuse)", "gray");

        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Missing Dependencies",
            "esptool is not installed.\n\n"
            "This tool requires Espressif's Python tools.\n\n"
            "Would you like to install it now?\n\n"
            "Command: pip install esptool",
            QMessageBox::Yes | QMessageBox::No
            );

        if (reply == QMessageBox::Yes) {
            installEspressifTools();
        }
        return;
    }
    appendLog("✅ esptool found (includes espsecure and espefuse)", "green");

    dependenciesChecked = true;
    appendLog("========================================", "green");
    appendLog("✅ Dependency check complete", "green");
    appendLog("========================================", "gray");
}

void MainWindow::installEspressifTools()
{
    appendLog("📦 Installing esptool (includes espsecure and espefuse)...", "gray");
    appendLog("Running: pip install esptool", "gray");

    QString python = getPythonPath();
    if (python.isEmpty()) {
        appendLog("❌ Python not found!", "red");
        return;
    }

    QProcess *pipProcess = new QProcess(this);
    if (!pipProcess) {
        appendLog("❌ Failed to create process!", "red");
        return;
    }

    QStringList args;
    args << "-m" << "pip" << "install" << "esptool";

    connect(pipProcess, &QProcess::readyReadStandardOutput, [this, pipProcess]() {
        if (!pipProcess) return;
        QString output = pipProcess->readAllStandardOutput();
        if (!output.isEmpty()) {
            appendLog(output, "gray");
        }
    });

    connect(pipProcess, &QProcess::readyReadStandardError, [this, pipProcess]() {
        if (!pipProcess) return;
        QString error = pipProcess->readAllStandardError();
        if (!error.isEmpty()) {
            appendLog(error, "yellow");
        }
    });

    connect(pipProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, pipProcess](int exitCode, QProcess::ExitStatus status) {
                if (!pipProcess) return;

                if (exitCode == 0 && status == QProcess::NormalExit) {
                    appendLog("✅ esptool installed successfully!", "green");
                    appendLog("   (Includes espsecure and espefuse)", "gray");
                    dependenciesChecked = true;

                    QMessageBox::information(this, "Installation Complete",
                                             "esptool has been installed successfully!\n\n"
                                             "This includes:\n"
                                             "• esptool.py\n"
                                             "• espsecure.py\n"
                                             "• espefuse.py\n\n"
                                             "You can now use the ESP32 Flasher.");

                    // ===== REFRESH PORTS AFTER INSTALLATION =====
                    appendLog("🔄 Refreshing ports to detect ESP32...", "gray");
                    onRefreshPorts();

                } else {
                    appendLog("❌ Installation failed!", "red");
                    appendLog("💡 Please install manually:", "yellow");
                    appendLog("   pip install esptool", "gray");
                    QMessageBox::warning(this, "Installation Failed",
                                         "Failed to install esptool.\n\n"
                                         "Please install it manually:\n"
                                         "  pip install esptool\n\n"
                                         "(This includes espsecure and espefuse)");
                }
                pipProcess->deleteLater();
            });

    pipProcess->start(python, args);

    if (!pipProcess->waitForStarted(3000)) {
        appendLog("❌ Failed to start pip installation!", "red");
        appendLog("💡 Please install manually: pip install esptool", "yellow");
        pipProcess->deleteLater();
    }
}

void MainWindow::populateBaudRates()
{
    QList<int> baudRates = {
        115200, 230400, 460800, 921600,
        1500000, 2000000, 3000000, 4000000
    };

    for (int rate : baudRates) {
        baudRateComboBox->addItem(QString::number(rate), rate);
    }

    int defaultIndex = baudRateComboBox->findData(921600);
    if (defaultIndex >= 0) {
        baudRateComboBox->setCurrentIndex(defaultIndex);
    }
}

void MainWindow::createConnections()
{
    connect(generateKeyBtn, &QPushButton::clicked,
            this, &MainWindow::onGenerateKey);
    connect(selectKeyBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectKey);
    connect(selectBootloaderBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectBootloader);
    connect(selectPartitionBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectPartition);
    connect(selectFirmwareBtn, &QPushButton::clicked,
            this, &MainWindow::onSelectFirmware);
    connect(flashBtn, &QPushButton::clicked,
            this, &MainWindow::onFlash);
    connect(eraseFlashBtn, &QPushButton::clicked,
            this, &MainWindow::onEraseFlash);
    connect(encryptFlashCheckBox, &QCheckBox::stateChanged,
            this, &MainWindow::updateFlashButton);

    connect(bootloaderFileEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateFlashButton);
    connect(partitionFileEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateFlashButton);
    connect(firmwareFileEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateFlashButton);
}

void MainWindow::setupAutoSave()
{
    connect(keyFileEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    connect(bootloaderFileEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    connect(partitionFileEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    connect(firmwareFileEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);

    connect(bootloaderOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::saveSettings);
    connect(partitionOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::saveSettings);
    connect(firmwareOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::saveSettings);

    connect(baudRateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::saveSettings);

    connect(encryptFlashCheckBox, &QCheckBox::stateChanged, this, &MainWindow::saveSettings);
    connect(compressCheckBox, &QCheckBox::stateChanged, this, &MainWindow::saveSettings);
    connect(portComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::saveSettings);
}

void MainWindow::loadSettings()
{
    isSettingLoaded = false;

    selectedKeyPath = settings->value("keyPath", "").toString();
    selectedBootloaderPath = settings->value("bootloaderPath", "").toString();
    selectedPartitionPath = settings->value("partitionPath", "").toString();
    selectedFirmwarePath = settings->value("firmwarePath", "").toString();

    bootloaderOffsetSpin->setValue(settings->value("bootloaderOffset", 0x1000).toInt());
    partitionOffsetSpin->setValue(settings->value("partitionOffset", 0x8000).toInt());
    firmwareOffsetSpin->setValue(settings->value("firmwareOffset", 0x10000).toInt());

    int baudRate = settings->value("baudRate", 921600).toInt();
    int baudIndex = baudRateComboBox->findData(baudRate);
    if (baudIndex >= 0) {
        baudRateComboBox->setCurrentIndex(baudIndex);
    }

    encryptFlashCheckBox->setChecked(settings->value("encryptFlash", false).toBool());
    compressCheckBox->setChecked(settings->value("compress", true).toBool());

    QString lastPort = settings->value("lastPort", "").toString();
    int portIndex = portComboBox->findText(lastPort);
    if (portIndex >= 0) {
        portComboBox->setCurrentIndex(portIndex);
    }

    keyFileEdit->setText(selectedKeyPath);
    bootloaderFileEdit->setText(selectedBootloaderPath);
    partitionFileEdit->setText(selectedPartitionPath);
    firmwareFileEdit->setText(selectedFirmwarePath);

    isSettingLoaded = true;
    appendLog("Settings loaded", "gray");
    updateFlashButton();
}

void MainWindow::saveSettings()
{
    if (!isSettingLoaded) {
        return;
    }

    settings->setValue("keyPath", selectedKeyPath);
    settings->setValue("bootloaderPath", selectedBootloaderPath);
    settings->setValue("partitionPath", selectedPartitionPath);
    settings->setValue("firmwarePath", selectedFirmwarePath);

    settings->setValue("bootloaderOffset", bootloaderOffsetSpin->value());
    settings->setValue("partitionOffset", partitionOffsetSpin->value());
    settings->setValue("firmwareOffset", firmwareOffsetSpin->value());

    settings->setValue("baudRate", baudRateComboBox->currentData().toInt());

    settings->setValue("encryptFlash", encryptFlashCheckBox->isChecked());
    settings->setValue("compress", compressCheckBox->isChecked());

    if (portComboBox->currentIndex() >= 0) {
        settings->setValue("lastPort", portComboBox->currentText());
    }

    settings->sync();
}

void MainWindow::updateFlashButton()
{
    if (!encryptionSetupInProgress && !isFlashing) {
        pendingEncryptionSteps.clear();
    }

    bool hasFiles = !selectedFirmwarePath.isEmpty() ||
                    !selectedBootloaderPath.isEmpty() ||
                    !selectedPartitionPath.isEmpty();

    bool portAvailable = portComboBox->count() > 0;

    bool keyValid = true;
    if (encryptFlashCheckBox->isChecked()) {
        keyValid = !selectedKeyPath.isEmpty() && QFile::exists(selectedKeyPath);
    }

    bool enabled = !isFlashing && hasFiles && portAvailable && keyValid;

    flashBtn->setEnabled(enabled);
}

void MainWindow::updateEncryptionStatusLabel(bool configured, const QString &details)
{
    isEncryptionConfigured = configured;
    if (configured) {
        encryptionStatusLabel->setText("🔒 Encryption: Enabled ✓");
        encryptionStatusLabel->setStyleSheet("color: #51cf66; font-weight: bold;");
        eraseFlashBtn->setEnabled(true);
        eraseFlashBtn->setToolTip("Erase flash (encryption is permanent)");
    } else {
        encryptionStatusLabel->setText("🔓 Encryption: Disabled");
        encryptionStatusLabel->setStyleSheet("color: #ff6b6b; font-weight: bold;");
        eraseFlashBtn->setEnabled(true);
        eraseFlashBtn->setToolTip("Erase flash memory");
    }
}

QString MainWindow::formatOffset(quint32 offset)
{
    return QString("0x%1").arg(offset, 8, 16, QChar('0'));
}

void MainWindow::parseChipInfo(const QString &output)
{
    QRegularExpression chipRegex("Chip type:\\s*([A-Za-z0-9\\-]+)\\s*\\(revision\\s*([^)]+)\\)");
    QRegularExpressionMatch match = chipRegex.match(output);
    if (match.hasMatch()) {
        chipModelLabel->setText("Model: " + match.captured(1));
        chipRevisionLabel->setText("Revision: " + match.captured(2));
    }

    QRegularExpression featuresRegex("Features:\\s*([^\r\n]+)");
    match = featuresRegex.match(output);
    if (match.hasMatch()) {
        QString feats = match.captured(1).trimmed();
        chipFeaturesLabel->setText("Features: " + feats);
        chipCoresLabel->setText(feats.contains("Dual Core") ? "Cores: 2" : "Cores: 1");
    }

    QRegularExpression macRegex("MAC:\\s*([0-9A-Fa-f:]+)");
    match = macRegex.match(output);
    if (match.hasMatch()) {
        chipMacLabel->setText("MAC Address: " + match.captured(1));
    }
}

void MainWindow::parseFlashSize(const QString &output)
{
    QRegularExpression flashRegex("Detected flash size:\\s*(\\S+)");
    QRegularExpressionMatch match = flashRegex.match(output);
    if (match.hasMatch()) {
        flashSizeLabel->setText("Flash Size: " + match.captured(1));
        flashSizeReceived = true;
        appendLog("✅ Flash size detected: " + match.captured(1), "green");
        return;
    }

    QRegularExpression flashRegex2("Flash size:\\s*(\\S+)");
    match = flashRegex2.match(output);
    if (match.hasMatch()) {
        flashSizeLabel->setText("Flash Size: " + match.captured(1));
        flashSizeReceived = true;
        appendLog("✅ Flash size detected: " + match.captured(1), "green");
        return;
    }

    QRegularExpression flashRegex3("(\\d+)\\s*(MB|KB)");
    match = flashRegex3.match(output);
    if (match.hasMatch()) {
        flashSizeLabel->setText("Flash Size: " + match.captured(1) + match.captured(2));
        flashSizeReceived = true;
        appendLog("✅ Flash size detected: " + match.captured(1) + match.captured(2), "green");
    }
}

void MainWindow::parseEncryptionStatus(const QString &output)
{
    bool encryptionDetected = false;
    bool keyProgrammed = false;
    bool configSet = false;
    bool cryptCntSet = false;

    if (output.contains("BLOCK1 (BLOCK1)") &&
        output.contains("Flash encryption key")) {
        QRegularExpression keyRegex("BLOCK1.*?=\\s*([?a-fA-F0-9 ]+)");
        QRegularExpressionMatch match = keyRegex.match(output);
        if (match.hasMatch()) {
            QString keyData = match.captured(1).trimmed();
            if (keyData.contains("?") || !keyData.replace(" ", "").contains("00")) {
                keyProgrammed = true;
                appendLog("🔑 Encryption key is burned and read-protected", "green");
            }
        }
    }

    if (output.contains("FLASH_CRYPT_CONFIG")) {
        QRegularExpression configRegex("FLASH_CRYPT_CONFIG.*?=\\s*(0x[0-9a-fA-F]+)");
        QRegularExpressionMatch match = configRegex.match(output);
        if (match.hasMatch()) {
            QString configValue = match.captured(1);
            if (configValue != "0x0") {
                configSet = true;
            }
        }
    }

    if (output.contains("FLASH_CRYPT_CNT")) {
        QRegularExpression cntRegex("FLASH_CRYPT_CNT.*?=\\s*(\\d+)");
        QRegularExpressionMatch match = cntRegex.match(output);
        if (match.hasMatch()) {
            int cnt = match.captured(1).toInt();
            if (cnt > 0) {
                cryptCntSet = true;
                encryptionDetected = true;
                appendLog("🔒 FLASH_CRYPT_CNT = " + QString::number(cnt) + " (encryption is ENABLED)", "green");
            } else {
                appendLog("🔓 FLASH_CRYPT_CNT = 0 (encryption is DISABLED)", "yellow");
                encryptionDetected = false;
            }
        }
    }

    if (output.contains("Flash encryption key is programmed") ||
        output.contains("FLASH_CRYPT_CNT: 0b")) {
        if (!cryptCntSet) {
            encryptionDetected = true;
        }
    }

    if (encryptionDetected || cryptCntSet) {
        updateEncryptionStatusLabel(true);
        isEncryptionConfigured = true;
        appendLog("🔒 Flash encryption is ENABLED on this device", "green");
        appendLog("⚠️ This encryption is PERMANENT and cannot be undone!", "red");
        eraseFlashBtn->setEnabled(true);
        eraseFlashBtn->setToolTip("Erase flash (encryption is permanent)");
        eraseFlashBtn->setText("🗑️ Erase Flash");
    } else {
        updateEncryptionStatusLabel(false);
        isEncryptionConfigured = false;
        appendLog("🔓 Flash encryption is DISABLED on this device", "yellow");
        if (keyProgrammed) {
            appendLog("ℹ️ Key is burned but encryption is NOT enabled (FLASH_CRYPT_CNT = 0)", "yellow");
            appendLog("ℹ️ To enable encryption, burn FLASH_CRYPT_CNT to 1", "yellow");
        } else {
            appendLog("ℹ️ Encryption can be configured if needed.", "gray");
        }
        eraseFlashBtn->setEnabled(true);
        eraseFlashBtn->setToolTip("Erase flash memory");
        eraseFlashBtn->setText("🗑️ Erase Flash");
    }
}

void MainWindow::parseFuseStatus(const QString &output)
{
    if (output.contains("UART_DOWNLOAD_DIS")) {
        QRegularExpression regex("UART_DOWNLOAD_DIS.*?=\\s*(\\w+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            if (value.contains("True") || value.contains("1")) {
                uartFuseStatus->setText("🔒 Disabled");
                uartFuseStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                uartFuseStatus->setText("🔓 Enabled");
                uartFuseStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("JTAG_DISABLE")) {
        QRegularExpression regex("JTAG_DISABLE.*?=\\s*(\\w+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            if (value.contains("True") || value.contains("1")) {
                jtagFuseStatus->setText("🔒 Disabled");
                jtagFuseStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                jtagFuseStatus->setText("🔓 Enabled");
                jtagFuseStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("DISABLE_DL_ENCRYPT")) {
        QRegularExpression regex("DISABLE_DL_ENCRYPT.*?=\\s*(\\w+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            if (value.contains("True") || value.contains("1")) {
                dlEncryptStatus->setText("🔒 Disabled");
                dlEncryptStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                dlEncryptStatus->setText("🔓 Enabled");
                dlEncryptStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("DISABLE_DL_DECRYPT")) {
        QRegularExpression regex("DISABLE_DL_DECRYPT.*?=\\s*(\\w+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            if (value.contains("True") || value.contains("1")) {
                dlDecryptStatus->setText("🔒 Disabled");
                dlDecryptStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                dlDecryptStatus->setText("🔓 Enabled");
                dlDecryptStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("DIS_CACHE")) {
        QRegularExpression regex("DIS_CACHE.*?=\\s*(\\w+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            if (value.contains("True") || value.contains("1")) {
                cacheStatus->setText("🔒 Disabled");
                cacheStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                cacheStatus->setText("🔓 Enabled");
                cacheStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("CONSOLE_DEBUG_DISABLE")) {
        QRegularExpression regex("CONSOLE_DEBUG_DISABLE.*?=\\s*(\\w+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            if (value.contains("True") || value.contains("1")) {
                consoleDebugStatus->setText("🔒 Disabled");
                consoleDebugStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                consoleDebugStatus->setText("🔓 Enabled");
                consoleDebugStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("FLASH_CRYPT_CNT")) {
        QRegularExpression regex("FLASH_CRYPT_CNT.*?=\\s*(\\d+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            int cnt = match.captured(1).toInt();
            flashCryptCntStatus->setText(QString::number(cnt));
            if (cnt > 0) {
                flashCryptCntStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                flashCryptCntStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }

    if (output.contains("FLASH_CRYPT_CONFIG")) {
        QRegularExpression regex("FLASH_CRYPT_CONFIG.*?=\\s*(0x[0-9a-fA-F]+)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            QString value = match.captured(1);
            flashCryptConfigStatus->setText(value);
            if (value != "0x0") {
                flashCryptConfigStatus->setStyleSheet("color: #dc3545; font-weight: bold;");
            } else {
                flashCryptConfigStatus->setStyleSheet("color: #51cf66; font-weight: bold;");
            }
        }
    }
}

void MainWindow::executeNextCommand()
{
    if (commandQueueIndex >= commandQueue.size()) {
        commandQueueRunning = false;
        commandQueueIndex = 0;
        commandQueue.clear();
        return;
    }

    QString command = commandQueue[commandQueueIndex];
    commandQueueIndex++;

    if (command == "flash-id") {
        appendLog("Reading chip information...", "gray");
        QStringList args;
        args << "--port" << portComboBox->currentText();
        args << "flash-id";
        pendingModule = "esptool_flashid";
        pendingArgs = args;
        runModuleCommand("esptool", args);
    } else if (command == "summary") {
        appendLog("Checking flash encryption status...", "gray");
        QStringList args;
        args << "--port" << portComboBox->currentText();
        args << "summary";
        pendingModule = "espefuse_summary";
        pendingArgs = args;
        runModuleCommand("espefuse", args);
    }
}

void MainWindow::onGenerateKey()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Save Encryption Key",
        QDir::homePath() + "/esp32_encryption_key.bin",
        "Binary Files (*.bin);;All Files (*)"
        );

    if (fileName.isEmpty()) {
        return;
    }

    QStringList args;
    args << "generate-flash-encryption-key" << fileName;

    pendingKeyPath = fileName;
    keyFileEdit->setText(fileName);
    appendLog(QString("Generating encryption key: %1").arg(fileName), "gray");
    appendLog("⏳ Please wait...", "gray");

    pendingModule = "espsecure";
    pendingArgs = args;
    runModuleCommand("espsecure", args);
}

void MainWindow::onSelectKey()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Select Encryption Key",
        QDir::homePath(),
        "Binary Files (*.bin);;All Files (*)"
        );

    if (!fileName.isEmpty()) {
        selectedKeyPath = fileName;
        keyFileEdit->setText(fileName);
        appendLog(QString("Selected key: %1").arg(fileName), "green");
    } else {
        selectedKeyPath.clear();
        keyFileEdit->clear();
        appendLog("Key selection cleared", "gray");
    }
    updateFlashButton();
}

void MainWindow::onSelectBootloader()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Select Bootloader Binary",
        QDir::homePath(),
        "Binary Files (*.bin);;All Files (*)"
        );

    if (!fileName.isEmpty()) {
        selectedBootloaderPath = fileName;
        bootloaderFileEdit->setText(fileName);
        appendLog(QString("Selected bootloader: %1").arg(fileName), "green");
    } else {
        selectedBootloaderPath.clear();
        bootloaderFileEdit->clear();
        appendLog("Bootloader selection cleared", "gray");
    }
    updateFlashButton();
}

void MainWindow::onSelectPartition()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Select Partition Table",
        QDir::homePath(),
        "Binary Files (*.bin);;All Files (*)"
        );

    if (!fileName.isEmpty()) {
        selectedPartitionPath = fileName;
        partitionFileEdit->setText(fileName);
        appendLog(QString("Selected partition table: %1").arg(fileName), "green");
    } else {
        selectedPartitionPath.clear();
        partitionFileEdit->clear();
        appendLog("Partition table selection cleared", "gray");
    }
    updateFlashButton();
}

void MainWindow::onSelectFirmware()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Select Firmware Image",
        QDir::homePath(),
        "Binary Files (*.bin);;All Files (*)"
        );

    if (!fileName.isEmpty()) {
        selectedFirmwarePath = fileName;
        firmwareFileEdit->setText(fileName);
        appendLog(QString("Selected firmware: %1").arg(fileName), "green");
    } else {
        selectedFirmwarePath.clear();
        firmwareFileEdit->clear();
        appendLog("Firmware selection cleared", "gray");
    }
    updateFlashButton();
}

void MainWindow::onClearAll()
{
    selectedBootloaderPath.clear();
    bootloaderFileEdit->clear();
    selectedPartitionPath.clear();
    partitionFileEdit->clear();
    selectedFirmwarePath.clear();
    firmwareFileEdit->clear();
    selectedKeyPath.clear();
    keyFileEdit->clear();
    appendLog("All selections cleared", "gray");
    updateFlashButton();
}

void MainWindow::onRefreshPorts()
{
    appendLog("========================================", "gray");
    appendLog("🔄 Refreshing ports and chip information...", "gray");
    appendLog("========================================", "gray");

    // Reset chip info
    chipModelLabel->setText("Model: --");
    chipRevisionLabel->setText("Revision: --");
    chipCoresLabel->setText("Cores: --");
    chipFeaturesLabel->setText("Features: --");
    chipMacLabel->setText("MAC Address: --");
    flashSizeLabel->setText("Flash Size: --");
    encryptionStatusLabel->setText("🔓 Encryption: Checking...");
    encryptionStatusLabel->setStyleSheet("color: #ffd43b; font-weight: bold;");
    chipInfoReceived = false;
    flashSizeReceived = false;
    appendLog("✅ Chip info reset", "gray");

    // Reset encryption status
    isEncryptionConfigured = false;
    appendLog("✅ Encryption status reset", "gray");

    // Clear command queue
    commandQueue.clear();
    commandQueueIndex = 0;
    commandQueueRunning = false;
    appendLog("✅ Command queue cleared", "gray");

    // Clear the port combobox
    portComboBox->clear();
    appendLog("✅ Port list cleared", "gray");

    // Stop serial monitor thread
    if (serialMonitor) {
        serialMonitor->stop();
        serialMonitor->wait();
        appendLog("✅ Serial monitor stopped", "gray");
    }

    // ===== DIRECT PORT SCAN ON MAIN THREAD =====
    // Get ports directly from the monitor's listPortsNow() method
    if (serialMonitor) {
        QList<SimplePortInfo> ports = serialMonitor->listPortsNow();
        appendLog(QString("🔍 Found %1 port(s)").arg(ports.size()), "gray");

        bool espFound = false;
        for (const SimplePortInfo &port : ports) {
            // Add port to combobox
            if (portComboBox->findText(port.portName) < 0) {
                portComboBox->addItem(port.portName);
            }

            // Check if this looks like an ESP32
            QString desc = port.description.toLower();
            if (desc.contains("cp210") ||
                desc.contains("ch340") ||
                desc.contains("ch341") ||
                desc.contains("ftdi")) {

                espFound = true;
                appendLog(QString("✅ Found ESP32 on port %1 (%2)").arg(port.portName, port.description), "green");

                // Set the current port
                int index = portComboBox->findText(port.portName);
                if (index >= 0) {
                    portComboBox->setCurrentIndex(index);
                }

                // Call onPortDetected directly
                onPortDetected(port);
                break;
            }
        }

        if (!espFound) {
            appendLog("ℹ️ No ESP32 found on any port", "yellow");
            portStatusLabel->setText("🔴 No ESP32 detected");
            portStatusLabel->setStyleSheet("font-weight: bold; color: red;");
        }
    }

    // Restart serial monitor thread
    if (serialMonitor) {
        serialMonitor->start();
        appendLog("✅ Serial monitor restarted", "gray");
    }

    updateStatus("Refreshed");
    updateFlashButton();

    appendLog("========================================", "green");
    appendLog("✅ Refresh complete", "green");
    appendLog("========================================", "gray");
}


void MainWindow::onFlash()
{
    if (isFlashing || encryptionSetupInProgress || isBurningFuses) {
        return;
    }

    logTextEdit->clear();
    appendLog("========================================", "gray");
    appendLog("🚀 Starting Flash Operation", "gray");
    appendLog("========================================", "gray");

    isFlashing = false;
    isCheckingEncryption = false;

    if (portComboBox->currentIndex() < 0) {
        appendLog("❌ Error: No ESP32 device detected!", "red");
        QMessageBox::warning(this, "Error", "No ESP32 device detected!");
        return;
    }

    if (selectedFirmwarePath.isEmpty() &&
        selectedBootloaderPath.isEmpty() &&
        selectedPartitionPath.isEmpty()) {
        appendLog("❌ Error: Please select at least one file to flash!", "red");
        QMessageBox::warning(this, "Error",
                             "Please select at least one file to flash!");
        return;
    }

    if (encryptFlashCheckBox->isChecked()) {
        if (selectedKeyPath.isEmpty() || !QFile::exists(selectedKeyPath)) {
            appendLog("❌ Error: Please select a valid encryption key file!", "red");
            QMessageBox::warning(this, "Error",
                                 "Please select a valid encryption key file!");
            return;
        }

        if (!isEncryptionConfigured) {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                "Encryption Setup Required",
                "Flash encryption is not configured on this ESP32.\n\n"
                "The application will now perform a 4-step setup:\n"
                "1. Burn encryption key to eFuses (IRREVERSIBLE!)\n"
                "2. Burn FLASH_CRYPT_CONFIG to 0xF (IRREVERSIBLE!)\n"
                "3. Burn FLASH_CRYPT_CNT to 1 (IRREVERSIBLE!)\n"
                "4. Flash your firmware with encryption\n"
                "   (The chip encrypts the firmware during write using the burned key)\n\n"
                "⚠️ WARNING: Steps 1-3 are a ONE-TIME operation!\n"
                "After burning, encryption is PERMANENT and cannot be undone.\n\n"
                "Keep your key file safe - losing it will brick the device!\n\n"
                "Do you want to proceed?",
                QMessageBox::Yes | QMessageBox::No
                );

            if (reply == QMessageBox::Yes) {
                performEncryptionSetup();
            }
            return;
        }
    }

    if (encryptFlashCheckBox->isChecked() && isEncryptionConfigured) {
        flashWithEncryption();
    } else {
        flashWithoutEncryption();
    }
}

void MainWindow::performEncryptionSetup()
{
    appendLog("========================================", "yellow");
    appendLog("🔐 Starting Flash Encryption Setup (4 Steps)", "yellow");
    appendLog("⚠️ THIS IS IRREVERSIBLE!", "red");
    appendLog("========================================", "yellow");

    // Step 1: Burn the key
    QStringList args;
    args << "--port" << portComboBox->currentText();
    args << "--do-not-confirm";
    args << "burn-key";
    args << "flash_encryption";
    args << selectedKeyPath;

    appendLog("Step 1/4: Burning encryption key to eFuses...", "yellow");
    appendLog("Key file: " + selectedKeyPath, "gray");
    appendLog("⚠️ Using --do-not-confirm flag (operation is still irreversible!)", "yellow");
    updateStatus("Burning encryption key...");
    enableControls(false);
    isFlashing = true;
    encryptionSetupInProgress = true;
    encryptionStepIndex = 0;
    keyBurnSkipped = false;

    progressBar->setVisible(true);
    progressBar->setRange(0, 4);
    progressBar->setValue(1);

    pendingEncryptionSteps.clear();
    pendingEncryptionSteps << "burn_key" << "burn_config" << "burn_cnt" << "flash_encrypted";

    pendingModule = "espefuse";
    pendingArgs = args;
    runModuleCommand("espefuse", args);
}

void MainWindow::handleEncryptionStepComplete()
{
    encryptionStepIndex++;

    if (encryptionStepIndex < pendingEncryptionSteps.size()) {
        appendLog("DEBUG: Next step: " + pendingEncryptionSteps[encryptionStepIndex], "gray");
    }

    if (encryptionStepIndex >= pendingEncryptionSteps.size()) {
        appendLog("✅ Flash encryption setup complete!", "green");
        appendLog("========================================", "yellow");
        isEncryptionConfigured = true;
        updateEncryptionStatusLabel(true);
        encryptionSetupInProgress = false;
        isFlashing = false;
        progressBar->setVisible(false);
        enableControls(true);

        cleanupEncryptedFile();

        QMessageBox::information(this, "Encryption Setup Complete",
                                 "Flash encryption has been configured successfully!\n\n"
                                 "The encrypted firmware has been flashed.\n\n"
                                 "⚠️ IMPORTANT: Keep your key file safe!\n"
                                 "If you lose the key, the device will be permanently bricked.");
        return;
    }

    QString step = pendingEncryptionSteps[encryptionStepIndex];

    if (step == "burn_config") {
        QStringList args;
        args << "--port" << portComboBox->currentText();
        args << "--do-not-confirm";
        args << "burn-efuse";
        args << "FLASH_CRYPT_CONFIG";
        args << "0xF";

        appendLog("Step 2/5: Burning FLASH_CRYPT_CONFIG to 0xF...", "yellow");
        appendLog("⚠️ THIS IS IRREVERSIBLE!", "red");
        updateStatus("Configuring encryption...");
        progressBar->setValue(2);

        pendingModule = "espefuse";
        pendingArgs = args;
        runModuleCommand("espefuse", args);

    } else if (step == "burn_cnt") {
        QStringList args;
        args << "--port" << portComboBox->currentText();
        args << "--do-not-confirm";
        args << "burn-efuse";
        args << "FLASH_CRYPT_CNT";
        args << "1";

        appendLog("Step 3/5: Burning FLASH_CRYPT_CNT to 1 to enable encryption...", "yellow");
        appendLog("⚠️ THIS IS THE FINAL IRREVERSIBLE STEP!", "red");
        updateStatus("Enabling encryption...");
        progressBar->setValue(3);

        pendingModule = "espefuse";
        pendingArgs = args;
        runModuleCommand("espefuse", args);

    } else if (step == "encrypt_firmware") {
        // SKIP THIS STEP - we don't need to pre-encrypt
        // The chip encrypts during the flash with --encrypt
        appendLog("DEBUG: Skipping host-side encryption (chip will encrypt during flash)", "gray");
        // Move to next step immediately
        handleEncryptionStepComplete();

    } else if (step == "flash_encrypted") {
        // Flash the ORIGINAL firmware with --encrypt
        if (selectedFirmwarePath.isEmpty() || !QFile::exists(selectedFirmwarePath)) {
            appendLog("❌ No firmware file selected!", "red");
            encryptionSetupInProgress = false;
            isFlashing = false;
            progressBar->setVisible(false);
            enableControls(true);
            return;
        }

        QStringList args;
        args << "--chip" << "esp32";
        args << "--port" << portComboBox->currentText();
        args << "--baud" << QString::number(baudRateComboBox->currentData().toInt());
        args << "--before" << "default-reset";
        args << "--after" << "hard-reset";
        args << "write-flash";
        args << "--encrypt";
        args << "-z";
        args << "--flash-mode" << getFlashMode();
        args << "--flash-freq" << getFlashFreq();
        args << "--flash-size" << getFlashSize();

        if (!selectedBootloaderPath.isEmpty()) {
            args << QString::number(bootloaderOffsetSpin->value());
            args << selectedBootloaderPath;
            appendLog(QString("Adding bootloader at offset %1: %2")
                          .arg(formatOffset(bootloaderOffsetSpin->value()),
                               QFileInfo(selectedBootloaderPath).fileName()), "gray");
        }

        if (!selectedPartitionPath.isEmpty()) {
            args << QString::number(partitionOffsetSpin->value());
            args << selectedPartitionPath;
            appendLog(QString("Adding partition table at offset %1: %2")
                          .arg(formatOffset(partitionOffsetSpin->value()),
                               QFileInfo(selectedPartitionPath).fileName()), "gray");
        }

        // Use ORIGINAL firmware (NOT pre-encrypted)
        args << QString::number(firmwareOffsetSpin->value());
        args << selectedFirmwarePath;

        appendLog("Step 5/5: Flashing encrypted firmware...", "yellow");
        appendLog("Firmware: " + selectedFirmwarePath, "gray");
        appendLog("💡 Chip will encrypt firmware using BLOCK1 key during write", "green");
        appendLog(QString("Flash mode: %1, Freq: %2, Size: %3")
                      .arg(getFlashMode(), getFlashFreq(), getFlashSize()), "gray");
        updateStatus("Flashing encrypted firmware...");
        progressBar->setValue(5);

        pendingModule = "esptool";
        pendingArgs = args;
        runModuleCommand("esptool", args);
    }
}

void MainWindow::encryptFirmwareWithKey()
{
    if (selectedFirmwarePath.isEmpty() || !QFile::exists(selectedFirmwarePath)) {
        appendLog("❌ No firmware selected for encryption!", "red");
        encryptionSetupInProgress = false;
        isFlashing = false;
        progressBar->setVisible(false);
        enableControls(true);
        return;
    }

    if (selectedKeyPath.isEmpty() || !QFile::exists(selectedKeyPath)) {
        appendLog("❌ No key file selected!", "red");
        encryptionSetupInProgress = false;
        isFlashing = false;
        progressBar->setVisible(false);
        enableControls(true);
        return;
    }

    encryptedFirmwarePath = getEncryptedFirmwarePath();

    appendLog("Step 4/5: Encrypting firmware with key (host-side)...", "yellow");
    appendLog("Source: " + selectedFirmwarePath, "gray");
    appendLog("Target: " + encryptedFirmwarePath, "gray");
    appendLog("💡 This is for first-time encryption setup", "gray");

    QStringList args;
    args << "encrypt-flash-data";
    args << "--keyfile" << selectedKeyPath;
    args << "--address" << QString::number(firmwareOffsetSpin->value());
    args << "--output" << encryptedFirmwarePath;
    args << selectedFirmwarePath;

    pendingModule = "espsecure";
    pendingArgs = args;
    runModuleCommand("espsecure", args);
}

QString MainWindow::getEncryptedFirmwarePath()
{
    QFileInfo fi(selectedFirmwarePath);
    QString basePath = fi.absolutePath();
    QString baseName = fi.baseName();
    QString extension = fi.suffix();

    return basePath + "/" + baseName + "_encrypted." + extension;
}

void MainWindow::cleanupEncryptedFile()
{
    if (!encryptedFirmwarePath.isEmpty() && QFile::exists(encryptedFirmwarePath)) {
        QFile::remove(encryptedFirmwarePath);
        appendLog("Cleaned up encrypted firmware file.", "gray");
        encryptedFirmwarePath.clear();
    }
}

void MainWindow::checkAndHandleKeyBurned()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Key Already Burned",
        "BLOCK1 is read-protected, which means a flash encryption key has already been burned.\n\n"
        "This is normal - the key is already in the eFuses.\n\n"
        "Do you want to skip key burning and proceed with encryption setup?\n"
        "This will:\n"
        "1. Burn FLASH_CRYPT_CONFIG to 0xF\n"
        "2. Burn FLASH_CRYPT_CNT to 1\n"
        "3. Encrypt your firmware\n"
        "4. Flash the encrypted firmware\n\n"
        "⚠️ This is still IRREVERSIBLE!\n\n"
        "Proceed?",
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply == QMessageBox::Yes) {
        keyBurnSkipped = true;
        pendingEncryptionSteps.removeAll("burn_key");
        encryptionStepIndex = 0;
        appendLog("DEBUG: After removing burn_key, steps remaining: " + pendingEncryptionSteps.join(", "), "gray");
        handleEncryptionStepComplete();
    } else {
        appendLog("Encryption setup cancelled by user.", "yellow");
        encryptionSetupInProgress = false;
        isFlashing = false;
        isCheckingEncryption = false;
        progressBar->setVisible(false);
        enableControls(true);
    }
}

void MainWindow::flashWithEncryption()
{
    appendLog("========================================", "green");
    appendLog("🔐 Flashing with Encryption", "green");
    appendLog("========================================", "green");

    if (isEncryptionConfigured) {
        appendLog("ℹ️ Chip is already encrypted. Using hardware encryption...", "gray");
        flashEncryptedFirmwareDirectly();
    } else {
        appendLog("ℹ️ First-time encryption setup. Running 5-step process...", "gray");
        performEncryptionSetup();
    }
}

void MainWindow::flashEncryptedFirmwareDirectly()
{
    appendLog("========================================", "green");
    appendLog("🔐 Flashing Encrypted Firmware (Hardware Encryption)", "green");
    appendLog("========================================", "green");

    appendLog("💡 Using chip's BLOCK1 key for encryption (no host-side encryption needed)", "gray");
    appendLog("💡 This is the modern, faster workflow for already-encrypted chips", "gray");

    appendLog("🔄 Resetting ESP32...", "gray");
    resetESP32();

    QStringList args;
    args << "--chip" << "esp32";
    args << "--port" << portComboBox->currentText();
    args << "--baud" << QString::number(baudRateComboBox->currentData().toInt());
    args << "--before" << "default-reset";
    args << "--after" << "hard-reset";
    args << "write-flash";
    args << "--encrypt";
    args << "-z";
    args << "--flash-mode" << getFlashMode();
    args << "--flash-freq" << getFlashFreq();
    args << "--flash-size" << getFlashSize();

    if (!selectedBootloaderPath.isEmpty()) {
        args << QString::number(bootloaderOffsetSpin->value());
        args << selectedBootloaderPath;
        appendLog(QString("Adding bootloader at offset %1: %2 (will be encrypted by chip)")
                      .arg(formatOffset(bootloaderOffsetSpin->value()),
                           QFileInfo(selectedBootloaderPath).fileName()), "gray");
    }

    if (!selectedPartitionPath.isEmpty()) {
        args << QString::number(partitionOffsetSpin->value());
        args << selectedPartitionPath;
        appendLog(QString("Adding partition table at offset %1: %2 (will be encrypted by chip)")
                      .arg(formatOffset(partitionOffsetSpin->value()),
                           QFileInfo(selectedPartitionPath).fileName()), "gray");
    }

    if (!selectedFirmwarePath.isEmpty()) {
        args << QString::number(firmwareOffsetSpin->value());
        args << selectedFirmwarePath;
        appendLog(QString("Adding firmware at offset %1: %2 (will be encrypted by chip using BLOCK1 key)")
                      .arg(formatOffset(firmwareOffsetSpin->value()),
                           QFileInfo(selectedFirmwarePath).fileName()), "gray");
    }

    appendLog(QString("Starting encrypted flash on port %1 at %2 baud...")
                  .arg(portComboBox->currentText())
                  .arg(baudRateComboBox->currentText()));
    appendLog("🔑 Chip will encrypt ALL data using the key in BLOCK1", "green");
    appendLog("💡 No key file needed - the key is already in the chip's eFuse", "gray");
    appendLog(QString("Flash mode: %1, Freq: %2, Size: %3")
                  .arg(getFlashMode(), getFlashFreq(), getFlashSize()), "gray");
    updateStatus("Flashing with hardware encryption...");

    isFlashing = true;
    enableControls(false);

    progressBar->setVisible(true);
    progressBar->setRange(0, 0);

    flashRetryCount = 0;

    pendingModule = "esptool";
    pendingArgs = args;
    runModuleCommand("esptool", args);
}

QString MainWindow::getFlashMode()
{
    return "dio";
}

QString MainWindow::getFlashFreq()
{
    return "40m";
}

QString MainWindow::getFlashSize()
{
    if (flashSizeLabel->text().contains("8MB")) {
        return "8MB";
    } else if (flashSizeLabel->text().contains("16MB")) {
        return "16MB";
    } else if (flashSizeLabel->text().contains("4MB")) {
        return "4MB";
    } else if (flashSizeLabel->text().contains("2MB")) {
        return "2MB";
    }
    return "4MB";
}

void MainWindow::flashWithoutEncryption()
{
    appendLog("========================================", "gray");
    appendLog("📦 Flashing without Encryption", "gray");
    appendLog("========================================", "gray");

    appendLog("🔄 Resetting ESP32...", "gray");
    resetESP32();

    QStringList args;
    args << "--chip" << "esp32";
    args << "--port" << portComboBox->currentText();
    args << "--baud" << QString::number(baudRateComboBox->currentData().toInt());
    args << "--before" << "default-reset";
    args << "--after" << "hard-reset";
    args << "write-flash";
    args << "-z";
    args << "--flash-mode" << getFlashMode();
    args << "--flash-freq" << getFlashFreq();
    args << "--flash-size" << getFlashSize();

    if (!selectedBootloaderPath.isEmpty()) {
        args << QString::number(bootloaderOffsetSpin->value());
        args << selectedBootloaderPath;
        appendLog(QString("Adding bootloader at offset %1: %2")
                      .arg(formatOffset(bootloaderOffsetSpin->value()),
                           QFileInfo(selectedBootloaderPath).fileName()), "gray");
    }

    if (!selectedPartitionPath.isEmpty()) {
        args << QString::number(partitionOffsetSpin->value());
        args << selectedPartitionPath;
        appendLog(QString("Adding partition table at offset %1: %2")
                      .arg(formatOffset(partitionOffsetSpin->value()),
                           QFileInfo(selectedPartitionPath).fileName()), "gray");
    }

    if (!selectedFirmwarePath.isEmpty()) {
        args << QString::number(firmwareOffsetSpin->value());
        args << selectedFirmwarePath;
        appendLog(QString("Adding firmware at offset %1: %2")
                      .arg(formatOffset(firmwareOffsetSpin->value()),
                           QFileInfo(selectedFirmwarePath).fileName()), "gray");
    }

    appendLog(QString("Starting flash on port %1 at %2 baud...")
                  .arg(portComboBox->currentText())
                  .arg(baudRateComboBox->currentText()));
    appendLog(QString("Flash mode: %1, Freq: %2, Size: %3")
                  .arg(getFlashMode(), getFlashFreq(), getFlashSize()), "gray");
    updateStatus("Flashing...");

    isFlashing = true;
    enableControls(false);

    progressBar->setVisible(true);
    progressBar->setRange(0, 0);

    pendingModule = "esptool";
    pendingArgs = args;
    runModuleCommand("esptool", args);
}

void MainWindow::resetESP32()
{
    if (portComboBox->currentIndex() < 0) {
        appendLog("❌ No ESP32 device detected to reset!", "red");
        return;
    }

    appendLog(QString("🔄 Resetting ESP32 on port %1...").arg(portComboBox->currentText()), "gray");

    QStringList args;
    args << "--port" << portComboBox->currentText();
    args << "chip-id";

    QProcess *resetProcess = new QProcess(this);
    connect(resetProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [resetProcess](int exitCode, QProcess::ExitStatus exitStatus) {
                resetProcess->deleteLater();
            });

    QString python = getPythonPath();
    QStringList fullArgs;
    fullArgs << "-m" << "esptool" << args;

    resetProcess->start(python, fullArgs);
    resetProcess->waitForFinished(3000);
    appendLog("✅ Reset complete", "gray");
}

void MainWindow::onEraseFlash()
{
    if (isFlashing || encryptionSetupInProgress || isBurningFuses) {
        return;
    }

    logTextEdit->clear();
    appendLog("========================================", "gray");
    appendLog("🗑️ Starting Erase Flash Operation", "gray");
    appendLog("========================================", "gray");

    if (isEncryptionConfigured) {
        appendLog("⚠️ WARNING: Encryption is configured on this device!", "red");
        QMessageBox::warning(this, "Encryption is Permanent",
                             "Flash encryption is configured on this device.\n\n"
                             "⚠️ WARNING: The encryption eFuses have been burned.\n"
                             "This is PERMANENT and CANNOT be undone!\n\n"
                             "Erasing the flash will make the device UNBOOTABLE\n"
                             "until you flash encrypted firmware with the correct key.\n\n");
    }

    QString message = "This will erase the entire flash memory.\n\n";
    if (isEncryptionConfigured) {
        message += "⚠️ Encryption is ENABLED. After erasing, you MUST flash encrypted firmware with the correct key!";
    } else {
        message += "ℹ️ Encryption is NOT configured. This is safe to do.";
    }
    message += "\n\nDo you want to continue?";

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Erase Flash",
        message,
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        appendLog("ℹ️ Erase operation cancelled by user.", "yellow");
        return;
    }

    if (portComboBox->currentIndex() < 0) {
        appendLog("❌ Error: No ESP32 device detected!", "red");
        QMessageBox::warning(this, "Error", "No ESP32 device detected!");
        return;
    }

    QStringList args;
    args << "--port" << portComboBox->currentText();
    args << "--baud" << QString::number(baudRateComboBox->currentData().toInt());
    args << "erase-flash";

    if (isEncryptionConfigured) {
        appendLog("🔑 Erasing encrypted flash...", "yellow");
        appendLog("⚠️ Device will be unbootable until encrypted firmware is flashed!", "red");
    } else {
        appendLog("🧹 Erasing flash memory...", "yellow");
        appendLog("ℹ️ Flash will be erased. You can flash new firmware.", "gray");
    }

    updateStatus("Erasing flash...");
    enableControls(false);
    isFlashing = true;
    progressBar->setVisible(true);
    progressBar->setRange(0, 0);

    pendingModule = "esptool";
    pendingArgs = args;
    runModuleCommand("esptool", args);
}

void MainWindow::onReadFuseStatus()
{
    if (portComboBox->currentIndex() < 0) {
        QMessageBox::warning(this, "Error", "No ESP32 device detected!");
        return;
    }

    appendLog("Reading fuse status...", "gray");

    QStringList args;
    args << "--port" << portComboBox->currentText();
    args << "summary";

    pendingModule = "espefuse_status";
    pendingArgs = args;
    runModuleCommand("espefuse", args);
}

void MainWindow::onBurnSecurityFuses()
{
    if (portComboBox->currentIndex() < 0) {
        QMessageBox::warning(this, "Error", "No ESP32 device detected!");
        return;
    }

    if (isBurningFuses) {
        return;
    }

    if (!uartDownloadDisCheckBox->isChecked() &&
        !jtagDisableCheckBox->isChecked() &&
        !disableDlEncryptCheckBox->isChecked() &&
        !disableDlDecryptCheckBox->isChecked() &&
        !disableCacheCheckBox->isChecked() &&
        !consoleDebugDisableCheckBox->isChecked()) {
        QMessageBox::warning(this, "Error", "Please select at least one fuse to burn!");
        return;
    }

    QStringList fusesToBurn;
    if (uartDownloadDisCheckBox->isChecked()) fusesToBurn << "UART_DOWNLOAD_DIS";
    if (jtagDisableCheckBox->isChecked()) fusesToBurn << "JTAG_DISABLE";
    if (disableDlEncryptCheckBox->isChecked()) fusesToBurn << "DISABLE_DL_ENCRYPT";
    if (disableDlDecryptCheckBox->isChecked()) fusesToBurn << "DISABLE_DL_DECRYPT";
    if (disableCacheCheckBox->isChecked()) fusesToBurn << "DIS_CACHE";
    if (consoleDebugDisableCheckBox->isChecked()) fusesToBurn << "CONSOLE_DEBUG_DISABLE";

    QString fuseList = fusesToBurn.join("\n• ");
    QString message = "⚠️ DANGER: You are about to burn the following fuses:\n\n• " + fuseList +
                      "\n\nThis operation is PERMANENT and IRREVERSIBLE!\n\n" +
                      "After burning these fuses:\n" +
                      "• The features will be permanently disabled\n" +
                      "• The ESP32 will be permanently locked\n" +
                      "• These changes CANNOT be undone\n\n" +
                      "Are you absolutely sure you want to proceed?";

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "⚠️ Burn Fuses - IRREVERSIBLE!",
        message,
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        return;
    }

    QMessageBox::StandardButton reply2 = QMessageBox::question(
        this,
        "⚠️ FINAL CONFIRMATION - IRREVERSIBLE!",
        "This is your LAST CHANCE.\n\n"
        "Burning these fuses is PERMANENT and CANNOT BE UNDONE.\n\n"
        "The ESP32 will be permanently modified.\n\n"
        "Are you sure?",
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply2 != QMessageBox::Yes) {
        return;
    }

    fuseBurnQueue.clear();
    if (uartDownloadDisCheckBox->isChecked()) fuseBurnQueue << "UART_DOWNLOAD_DIS";
    if (jtagDisableCheckBox->isChecked()) fuseBurnQueue << "JTAG_DISABLE";
    if (disableDlEncryptCheckBox->isChecked()) fuseBurnQueue << "DISABLE_DL_ENCRYPT";
    if (disableDlDecryptCheckBox->isChecked()) fuseBurnQueue << "DISABLE_DL_DECRYPT";
    if (disableCacheCheckBox->isChecked()) fuseBurnQueue << "DIS_CACHE";
    if (consoleDebugDisableCheckBox->isChecked()) fuseBurnQueue << "CONSOLE_DEBUG_DISABLE";

    fuseBurnIndex = 0;
    isBurningFuses = true;
    burnNextFuse();
}

void MainWindow::burnNextFuse()
{
    if (fuseBurnIndex >= fuseBurnQueue.size()) {
        appendLog("✅ All selected fuses burned successfully!", "green");
        isBurningFuses = false;
        fuseBurnQueue.clear();
        enableControls(true);
        onReadFuseStatus();
        return;
    }

    QString fuseName = fuseBurnQueue[fuseBurnIndex];
    fuseBurnIndex++;

    appendLog("========================================", "red");
    appendLog("🔥 Burning fuse: " + fuseName, "red");
    appendLog("⚠️ THIS IS IRREVERSIBLE!", "red");
    appendLog("========================================", "red");

    QStringList args;
    args << "--port" << portComboBox->currentText();
    args << "--do-not-confirm";
    args << "burn-efuse";
    args << fuseName;

    pendingModule = "espefuse";
    pendingArgs = args;
    runModuleCommand("espefuse", args);
}

void MainWindow::onPortDetected(const SimplePortInfo &port)
{
    QString portName = port.portName;
    QString description = port.description;
    QString vidPid = (port.hasVendorIdentifier && port.hasProductIdentifier)
                         ? QString("%1:%2")
                               .arg(port.vendorIdentifier, 4, 16, QChar('0'))
                               .arg(port.productIdentifier, 4, 16, QChar('0'))
                         : QString("unknown");

    // Clear any stale command queue
    commandQueue.clear();
    commandQueueIndex = 0;
    commandQueueRunning = false;

    int index = portComboBox->findText(portName);
    if (index < 0) {
        portComboBox->addItem(portName);
        portComboBox->setCurrentIndex(portComboBox->count() - 1);
    } else {
        portComboBox->setCurrentIndex(index);
    }

    currentPort = portName;
    portStatusLabel->setText(QString("🟢 ESP32 detected on %1 (%2)").arg(portName, description));
    portStatusLabel->setStyleSheet("font-weight: bold; color: green;");

    appendLog(QString("ESP32 detected: %1 (%2) VID:PID=%3")
                  .arg(portName, description, vidPid), "green");

    chipModelLabel->setText("Model: Reading...");
    chipRevisionLabel->setText("Revision: Reading...");
    chipCoresLabel->setText("Cores: Reading...");
    chipFeaturesLabel->setText("Features: Reading...");
    chipMacLabel->setText("MAC Address: Reading...");
    flashSizeLabel->setText("Flash Size: Reading...");
    encryptionStatusLabel->setText("🔓 Encryption: Checking...");
    encryptionStatusLabel->setStyleSheet("color: #ffd43b; font-weight: bold;");
    chipInfoReceived = false;
    flashSizeReceived = false;

    // Build command queue - sequential execution
    commandQueue.clear();
    commandQueue << "flash-id" << "summary";
    commandQueueIndex = 0;
    commandQueueRunning = true;

    // Start executing commands sequentially
    executeNextCommand();

    updateFlashButton();
}

void MainWindow::onPortRemoved(const QString &portName)
{
    int index = portComboBox->findText(portName);
    if (index >= 0) {
        portComboBox->removeItem(index);
    }

    if (portComboBox->count() == 0) {
        portStatusLabel->setText("🔴 No ESP32 detected");
        portStatusLabel->setStyleSheet("font-weight: bold; color: red;");
        chipModelLabel->setText("Model: --");
        chipRevisionLabel->setText("Revision: --");
        chipCoresLabel->setText("Cores: --");
        chipFeaturesLabel->setText("Features: --");
        chipMacLabel->setText("MAC Address: --");
        flashSizeLabel->setText("Flash Size: --");
        encryptionStatusLabel->setText("🔓 Encryption: Disabled");
        encryptionStatusLabel->setStyleSheet("color: #ff6b6b; font-weight: bold;");
        chipInfoReceived = false;
        flashSizeReceived = false;
        commandQueueRunning = false;
        commandQueue.clear();
        commandQueueIndex = 0;
    }

    appendLog(QString("ESP32 disconnected: %1").arg(portName), "red");
    updateFlashButton();
}

void MainWindow::checkEncryptionStatus()
{
}

void MainWindow::onProcessOutput()
{
    if (!esptoolProcess) return;

    QByteArray data = esptoolProcess->readAllStandardOutput();
    QString chunk = QString::fromUtf8(data);
    processOutputBuffer += chunk;

    if (pendingModule == "esptool_flashid") {
        parseChipInfo(processOutputBuffer);
        parseFlashSize(processOutputBuffer);

        if (chipModelLabel->text() != "Model: Reading..." &&
            chipModelLabel->text() != "Model: --") {
            chipInfoReceived = true;
            appendLog("✅ Chip information received", "green");
        }
    }

    if (pendingModule == "espefuse_summary") {
        parseEncryptionStatus(processOutputBuffer);
        isCheckingEncryption = false;
        isFirstEncryptionCheck = false;
    }

    if (pendingModule == "espefuse_status") {
        parseFuseStatus(processOutputBuffer);
        parseEncryptionStatus(processOutputBuffer);
    }

    if (pendingModule != "espefuse_summary" || isFirstEncryptionCheck) {
        appendLog(chunk);
    }
}

void MainWindow::onProcessError()
{
    if (!esptoolProcess) return;

    QByteArray data = esptoolProcess->readAllStandardError();
    QString error = QString::fromUtf8(data);
    appendLog("Error: " + error, "red");

    if (error.contains("BLOCK1 is read-protected", Qt::CaseInsensitive)) {
        if (encryptionSetupInProgress && pendingModule == "espefuse" && pendingArgs.contains("burn-key")) {
            appendLog("ℹ️ Key is already burned in BLOCK1. Skipping key burning...", "yellow");
            checkAndHandleKeyBurned();
        } else {
            QMessageBox::information(this, "Key Already Burned",
                                     "BLOCK1 is read-protected, which means a flash encryption key has already been burned.\n\n"
                                     "This is normal if encryption is already configured or a key was previously burned.\n"
                                     "You can proceed with enabling encryption and flashing encrypted firmware.");
        }
        return;
    }

    if (error.contains("Deprecated", Qt::CaseInsensitive)) {
        appendLog("ℹ️ " + error.trimmed(), "yellow");
        return;
    }

    if (error.contains("Wrong flash encryption key", Qt::CaseInsensitive) ||
        error.contains("Flash encryption key mismatch", Qt::CaseInsensitive) ||
        error.contains("MAC mismatch", Qt::CaseInsensitive)) {
        QMessageBox::critical(this, "Wrong Encryption Key",
                              "esptool reported a flash encryption key mismatch.\n\n"
                              "The selected key does not match the key burned into this device's eFuses. "
                              "Select the correct key file and try again.");
    }
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);

    appendLog("DEBUG: Process finished - Module: " + pendingModule + ", Args: " + pendingArgs.join(" "), "gray");

    bool wasEncryptionCheck = (pendingModule == "espefuse_summary");
    bool wasFuseStatus = (pendingModule == "espefuse_status");
    bool wasFlashId = (pendingModule == "esptool_flashid");
    bool wasErase = (pendingModule == "esptool" && pendingArgs.contains("erase-flash"));
    bool wasKeyBurn = (pendingModule == "espefuse" && pendingArgs.contains("burn-key"));
    bool wasKeyGen = (pendingModule == "espsecure" && pendingArgs.contains("generate-flash-encryption-key"));
    bool wasBurnConfig = (pendingModule == "espefuse" && pendingArgs.contains("burn-efuse") && pendingArgs.contains("FLASH_CRYPT_CONFIG"));
    bool wasBurnCnt = (pendingModule == "espefuse" && pendingArgs.contains("burn-efuse") && pendingArgs.contains("FLASH_CRYPT_CNT"));
    bool wasEncrypt = (pendingModule == "espsecure" && pendingArgs.contains("encrypt-flash-data"));
    bool wasFlashEncrypted = (pendingModule == "esptool" && !pendingArgs.contains("erase-flash") && !pendingArgs.contains("flash-id") && pendingArgs.contains(encryptedFirmwarePath));
    bool wasNormalFlash = (pendingModule == "esptool" && !pendingArgs.contains("erase-flash") && !pendingArgs.contains("flash-id") && !pendingArgs.contains(encryptedFirmwarePath) && pendingArgs.contains("write-flash"));
    bool wasFuseBurn = (pendingModule == "espefuse" && pendingArgs.contains("burn-efuse") &&
                        (pendingArgs.contains("UART_DOWNLOAD_DIS") ||
                         pendingArgs.contains("JTAG_DISABLE") ||
                         pendingArgs.contains("DISABLE_DL_ENCRYPT") ||
                         pendingArgs.contains("DISABLE_DL_DECRYPT") ||
                         pendingArgs.contains("DIS_CACHE") ||
                         pendingArgs.contains("CONSOLE_DEBUG_DISABLE")));
    bool wasFailedFlash = (pendingModule == "esptool" && pendingArgs.contains("write-flash") && !success);
    bool wasFirstEncryptionSetup = (wasBurnConfig || wasBurnCnt) && success;

    appendLog(QString("DEBUG: wasEncrypt=%1, wasFlashEncrypted=%2, wasKeyBurn=%3, wasKeyGen=%4, wasNormalFlash=%5, wasFuseBurn=%6, wasFailedFlash=%7, wasFirstEncryptionSetup=%8, encryptionSetupInProgress=%9, isEncryptionConfigured=%10")
                  .arg(wasEncrypt).arg(wasFlashEncrypted).arg(wasKeyBurn).arg(wasKeyGen).arg(wasNormalFlash).arg(wasFuseBurn).arg(wasFailedFlash).arg(wasFirstEncryptionSetup).arg(encryptionSetupInProgress).arg(isEncryptionConfigured), "gray");

    if (wasFailedFlash && flashRetryCount < MAX_FLASH_RETRIES) {
        flashRetryCount++;
        appendLog(QString("⚠️ Flash failed (attempt %1 of %2). Retrying in 2 seconds...")
                      .arg(flashRetryCount).arg(MAX_FLASH_RETRIES), "yellow");
        appendLog("💡 Make sure the port is not being used by another application (serial monitor, etc.)", "gray");

        QTimer::singleShot(2000, this, [this]() {
            if (isEncryptionConfigured) {
                flashEncryptedFirmwareDirectly();
            } else if (encryptFlashCheckBox->isChecked()) {
                flashWithEncryption();
            } else {
                flashWithoutEncryption();
            }
        });

        if (esptoolProcess) {
            esptoolProcess->deleteLater();
            esptoolProcess = nullptr;
        }
        return;
    }

    if (wasKeyGen && success) {
        appendLog("✅ Key generated successfully!", "green");
        if (!pendingKeyPath.isEmpty() && QFile::exists(pendingKeyPath)) {
            selectedKeyPath = pendingKeyPath;
            keyFileEdit->setText(pendingKeyPath);
            appendLog("✅ Key ready to use: " + pendingKeyPath, "green");
            appendLog("💡 You can now use this key for encryption", "gray");
            pendingKeyPath.clear();
            updateFlashButton();
        } else {
            appendLog("⚠️ Key file not found at: " + pendingKeyPath, "yellow");
            keyFileEdit->clear();
            selectedKeyPath.clear();
        }

        if (esptoolProcess) {
            esptoolProcess->deleteLater();
            esptoolProcess = nullptr;
        }
        enableControls(true);
        return;
    }

    if (wasEncrypt && success) {
        appendLog("✅ Firmware encrypted successfully!", "green");

        if (isEncryptionConfigured) {
            appendLog("ℹ️ Encryption is already configured. Flashing encrypted firmware directly...", "gray");
            flashEncryptedFirmwareDirectly();
            pendingModule.clear();
            pendingArgs.clear();
            return;
        }

        if (!encryptionSetupInProgress) {
            appendLog("DEBUG: encryptionSetupInProgress was false, setting to true", "gray");
            encryptionSetupInProgress = true;
        }
        appendLog("DEBUG: Calling handleEncryptionStepComplete() to proceed to flash", "gray");
        appendLog("DEBUG: Current encryptionStepIndex = " + QString::number(encryptionStepIndex), "gray");
        appendLog("DEBUG: pendingEncryptionSteps size = " + QString::number(pendingEncryptionSteps.size()), "gray");

        if (pendingEncryptionSteps.isEmpty()) {
            appendLog("DEBUG: pendingEncryptionSteps is empty! Re-initializing...", "yellow");
            pendingEncryptionSteps << "burn_config" << "burn_cnt" << "encrypt_firmware" << "flash_encrypted";
            encryptionStepIndex = 0;
        }

        handleEncryptionStepComplete();
        pendingModule.clear();
        pendingArgs.clear();
        return;
    }

    if (encryptionSetupInProgress) {
        if (success) {
            if (wasFlashEncrypted) {
                appendLog("✅ Encrypted firmware flashed successfully!", "green");
                isEncryptionConfigured = true;
                updateEncryptionStatusLabel(true);
                encryptionSetupInProgress = false;
                isFlashing = false;
                progressBar->setVisible(false);
                cleanupEncryptedFile();

                appendLog("🔄 First-time encryption setup complete!", "green");
                appendLog("💡 Performing extra reset to ensure encryption initializes properly...", "gray");

                QTimer::singleShot(500, this, [this]() {
                    resetESP32();
                    appendLog("✅ Extra reset complete", "green");
                    appendLog("ℹ️ Your ESP32 is now encrypted and should boot normally.", "green");
                    appendLog("⚠️ IMPORTANT: Keep your key file safe! You'll need it for future flashes.", "yellow");
                });

                QMessageBox::information(this, "Encryption Setup Complete",
                                         "Flash encryption has been configured successfully!\n\n"
                                         "The encrypted firmware has been flashed.\n\n"
                                         "⚠️ IMPORTANT: Keep your key file safe!\n"
                                         "If you lose the key, the device will be permanently bricked.");

                pendingModule.clear();
                pendingArgs.clear();
                enableControls(true);
                return;
            } else {
                appendLog("DEBUG: Continuing to next step", "gray");
                handleEncryptionStepComplete();
                return;
            }
        } else {
            if (wasKeyBurn) {
                if (!keyBurnSkipped) {
                    appendLog("❌ Key burning failed. Checking if key is already burned...", "yellow");
                    checkAndHandleKeyBurned();
                } else {
                    appendLog("❌ Encryption setup failed!", "red");
                    encryptionSetupInProgress = false;
                    isFlashing = false;
                    isCheckingEncryption = false;
                    progressBar->setVisible(false);
                    cleanupEncryptedFile();

                    QMessageBox::critical(this, "Encryption Setup Failed",
                                          "Failed to configure flash encryption.\n\n"
                                          "Check the log for details.");
                    enableControls(true);
                }
            } else if (wasEncrypt) {
                appendLog("❌ Firmware encryption failed!", "red");
                encryptionSetupInProgress = false;
                isFlashing = false;
                progressBar->setVisible(false);
                cleanupEncryptedFile();

                QMessageBox::critical(this, "Encryption Failed",
                                      "Failed to encrypt firmware.\n\n"
                                      "Check that the key file is valid (32 bytes).");
                enableControls(true);
            } else if (wasFlashEncrypted) {
                appendLog("❌ Failed to flash encrypted firmware!", "red");
                encryptionSetupInProgress = false;
                isFlashing = false;
                progressBar->setVisible(false);
                cleanupEncryptedFile();

                QMessageBox::critical(this, "Flash Failed",
                                      "Failed to flash encrypted firmware.\n\n"
                                      "Check the connection and try again.");
                enableControls(true);
            } else {
                appendLog("❌ Encryption setup failed at step " + QString::number(encryptionStepIndex + 1) + "!", "red");
                encryptionSetupInProgress = false;
                isFlashing = false;
                isCheckingEncryption = false;
                progressBar->setVisible(false);
                cleanupEncryptedFile();

                QMessageBox::critical(this, "Encryption Setup Failed",
                                      "Failed to configure flash encryption.\n\n"
                                      "Check the log for details.\n"
                                      "Make sure:\n"
                                      "- ESP32 is properly connected\n"
                                      "- Key file is valid (32 bytes)");
                enableControls(true);
            }
        }

        if (esptoolProcess) {
            esptoolProcess->deleteLater();
            esptoolProcess = nullptr;
        }
        return;
    }

    if (isBurningFuses && wasFuseBurn) {
        if (success) {
            QString fuseName = pendingArgs[pendingArgs.size() - 1];
            appendLog("✅ Fuse " + fuseName + " burned successfully!", "green");
            burnNextFuse();
        } else {
            appendLog("❌ Failed to burn fuse!", "red");
            isBurningFuses = false;
            fuseBurnQueue.clear();
            enableControls(true);

            QMessageBox::critical(this, "Fuse Burn Failed",
                                  "Failed to burn the selected fuse.\n\n"
                                  "Check the log for details.");
        }

        if (esptoolProcess) {
            esptoolProcess->deleteLater();
            esptoolProcess = nullptr;
        }
        return;
    }

    progressBar->setVisible(false);
    isFlashing = false;
    pendingModule.clear();
    pendingArgs.clear();

    if (wasEncryptionCheck || wasFuseStatus) {
        isFirstEncryptionCheck = false;
    }

    if (success) {
        updateStatus("Completed Successfully");

        if (!wasEncryptionCheck && !wasFuseStatus) {
            appendLog("✅ Operation completed successfully!", "green");
        }

        if (wasErase) {
            appendLog("Flash erased successfully.", "green");
            if (isEncryptionConfigured) {
                appendLog("⚠️ Device is now unbootable. You must flash encrypted firmware with the correct key!", "red");
            } else {
                appendLog("ℹ️ Device is now blank. You can flash new firmware.", "green");
            }
        }

        if (wasNormalFlash) {
            appendLog("ℹ️ Flash completed. You can flash again if needed.", "gray");

            if (isEncryptionConfigured) {
                appendLog("🔄 Performing soft reset...", "gray");
                QTimer::singleShot(500, this, [this]() {
                    resetESP32();
                    appendLog("✅ Reset complete. ESP32 should boot normally.", "green");
                });
            }
        }

        if (wasFuseStatus) {
            appendLog("✅ Fuse status updated", "gray");
        }

        if (!isEncryptionConfigured) {
            eraseFlashBtn->setEnabled(true);
        } else {
            eraseFlashBtn->setEnabled(true);
            eraseFlashBtn->setText("🗑️ Erase Flash");
        }
    } else {
        updateStatus("Failed");
        if (!wasEncryptionCheck && !wasFuseStatus) {
            appendLog("❌ Operation failed with exit code: " + QString::number(exitCode), "red");
        }
        if (wasEncryptionCheck) {
            appendLog("Failed to read encryption status. Check connection.", "yellow");
        }
        if (wasFuseStatus) {
            appendLog("Failed to read fuse status. Check connection.", "yellow");
        }
    }

    if (esptoolProcess) {
        esptoolProcess->deleteLater();
        esptoolProcess = nullptr;
    }

    if (commandQueueRunning && (wasFlashId || wasEncryptionCheck)) {
        QTimer::singleShot(500, this, &MainWindow::executeNextCommand);
    } else {
        enableControls(true);
    }
}

void MainWindow::updateProgress()
{
}

void MainWindow::appendLog(const QString &message, const QString &color)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formattedMessage;

    if (color == "red") {
        formattedMessage = QString("<span style='color: #ff6b6b;'>[%1] %2</span>")
        .arg(timestamp, message);
    } else if (color == "green") {
        formattedMessage = QString("<span style='color: #51cf66;'>[%1] %2</span>")
        .arg(timestamp, message);
    } else if (color == "yellow") {
        formattedMessage = QString("<span style='color: #ffd43b;'>[%1] %2</span>")
        .arg(timestamp, message);
    } else if (color == "gray") {
        formattedMessage = QString("<span style='color: #868e96;'>[%1] %2</span>")
        .arg(timestamp, message);
    } else {
        formattedMessage = QString("[%1] %2").arg(timestamp, message);
    }

    logTextEdit->append(formattedMessage);
    logTextEdit->verticalScrollBar()->setValue(
        logTextEdit->verticalScrollBar()->maximum()
        );
}

void MainWindow::runModuleCommand(const QString &module, const QStringList &args)
{
    if (esptoolProcess) {
        esptoolProcess->disconnect();
        if (esptoolProcess->state() == QProcess::Running) {
            esptoolProcess->terminate();
            if (!esptoolProcess->waitForFinished(2000)) {
                esptoolProcess->kill();
                esptoolProcess->waitForFinished(1000);
            }
        }
        esptoolProcess->deleteLater();
        esptoolProcess = nullptr;
    }

    processOutputBuffer.clear();

    esptoolProcess = new QProcess(this);
    connect(esptoolProcess, &QProcess::readyReadStandardOutput,
            this, &MainWindow::onProcessOutput);
    connect(esptoolProcess, &QProcess::readyReadStandardError,
            this, &MainWindow::onProcessError);
    connect(esptoolProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &MainWindow::onProcessFinished);

    QString python = getPythonPath();
    QStringList fullArgs;
    fullArgs << "-m" << module << args;

    if (module != "espefuse" || !args.contains("summary") || (pendingModule != "espefuse_summary" && pendingModule != "espefuse_status")) {
        appendLog("Running: " + python + " " + fullArgs.join(" "), "gray");
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    esptoolProcess->setProcessEnvironment(env);
    esptoolProcess->start(python, fullArgs);

    if (!esptoolProcess->waitForStarted(5000)) {
        appendLog(QString("Failed to start python -m %1.").arg(module), "red");
        QMessageBox::critical(this, "Error",
                              QString("Failed to start python -m %1\n\nMake sure Python is on PATH and the module is installed:\npip install %2").arg(module, module));
        enableControls(true);
        isFlashing = false;
        isCheckingEncryption = false;
        progressBar->setVisible(false);

        if (esptoolProcess) {
            esptoolProcess->deleteLater();
            esptoolProcess = nullptr;
        }
    }
}

QString MainWindow::getPythonPath()
{
    QStringList pythonExecutables;
    pythonExecutables << "python3" << "python" << "py" << "python3.11" << "python3.10" << "python3.9";

    for (const QString &pyExe : pythonExecutables) {
        QProcess process;
        process.start(pyExe, QStringList() << "--version");
        if (process.waitForFinished(2000)) {
            QString output = process.readAllStandardOutput();
            if (output.contains("Python 3")) {
                return pyExe;
            }
        }
    }

    return QString();
}

bool MainWindow::checkPythonAvailable()
{
    return !getPythonPath().isEmpty();
}

bool MainWindow::checkModuleAvailable(const QString &module)
{
    QString python = getPythonPath();
    if (python.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(python, QStringList() << "-c" << QString("import %1").arg(module));
    process.waitForFinished(3000);

    return process.exitCode() == 0;
}

void MainWindow::updateStatus(const QString &status)
{
    statusLabel->setText(status);
}

void MainWindow::enableControls(bool enable)
{
    generateKeyBtn->setEnabled(enable);
    selectKeyBtn->setEnabled(enable);
    selectBootloaderBtn->setEnabled(enable);
    selectPartitionBtn->setEnabled(enable);
    selectFirmwareBtn->setEnabled(enable);
    burnSecurityFusesBtn->setEnabled(enable);
    readFuseStatusBtn->setEnabled(enable);

    updateFlashButton();

    encryptFlashCheckBox->setEnabled(enable);
    portComboBox->setEnabled(enable);
    bootloaderOffsetSpin->setEnabled(enable);
    partitionOffsetSpin->setEnabled(enable);
    firmwareOffsetSpin->setEnabled(enable);
    baudRateComboBox->setEnabled(enable);
    compressCheckBox->setEnabled(enable);

    uartDownloadDisCheckBox->setEnabled(enable);
    jtagDisableCheckBox->setEnabled(enable);
    disableDlEncryptCheckBox->setEnabled(enable);
    disableDlDecryptCheckBox->setEnabled(enable);
    disableCacheCheckBox->setEnabled(enable);
    consoleDebugDisableCheckBox->setEnabled(enable);
}