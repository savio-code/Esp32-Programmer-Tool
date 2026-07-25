#include "EsptoolRunner.h"
#include <QRegularExpression>

EsptoolRunner::EsptoolRunner(QObject *parent) : QObject(parent) {
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &EsptoolRunner::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &EsptoolRunner::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &EsptoolRunner::onProcessFinished);
}

void EsptoolRunner::setPythonPath(const QString &path) { m_pythonPath = path; }
void EsptoolRunner::setPort(const QString &port) { m_port = port; }
void EsptoolRunner::setEsptoolPath(const QString &) { /* reserved, using -m invocation below */ }

bool EsptoolRunner::isRunning() const {
    return m_process->state() != QProcess::NotRunning;
}

void EsptoolRunner::abort() {
    if (isRunning()) m_process->kill();
}

void EsptoolRunner::startProcess(const QStringList &args) {
    if (isRunning()) {
        emit logLine("[!] A process is already running.");
        return;
    }
    m_accumulatedOutput.clear();
    emit logLine("$ " + m_pythonPath + " " + args.join(" "));
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start(m_pythonPath, args);
}

void EsptoolRunner::readChipInfo() {
    m_currentOp = Op::ChipInfo;
    QStringList args;
    args << "-m" << "esptool" << "--port" << m_port << "chip_id";
    startProcess(args);
}

void EsptoolRunner::detectEncryptionState() {
    m_currentOp = Op::DetectEncryption;
    QStringList args;
    args << "-m" << "espefuse" << "--port" << m_port << "summary";
    startProcess(args);
}

void EsptoolRunner::flashImage(const QString &imagePath, quint32 offset, bool encrypt) {
    m_currentOp = Op::Flash;
    QStringList args;
    args << "-m" << "esptool" << "--port" << m_port << "write_flash";
    if (encrypt) {
        args << "--encrypt";
    }
    args << QString("0x%1").arg(offset, 0, 16) << imagePath;
    startProcess(args);
}

void EsptoolRunner::burnEncryptionFuses() {
    m_currentOp = Op::BurnFuses;
    QStringList args;
    // Irreversible. UI must confirm with the user before this is ever called.
    args << "-m" << "espefuse" << "--port" << m_port
         << "burn_efuse" << "FLASH_CRYPT_CNT";
    startProcess(args);
}

void EsptoolRunner::onReadyReadStdout() {
    QByteArray data = m_process->readAllStandardOutput();
    QString chunk = QString::fromLocal8Bit(data);
    m_accumulatedOutput += chunk;
    const auto lines = chunk.split('\n', Qt::SkipEmptyParts);
    for (const auto &line : lines) {
        emit logLine(line);

        static QRegularExpression re(R"(\((\d+)\s*%\))");
        auto m = re.match(line);
        if (m.hasMatch()) emit progress(m.captured(1).toInt());

        if (line.contains("Wrong flash encryption key", Qt::CaseInsensitive) ||
            line.contains("Flash encryption key mismatch", Qt::CaseInsensitive) ||
            line.contains("MAC mismatch", Qt::CaseInsensitive)) {
            emit wrongKeyDetected();
        }
    }
}

void EsptoolRunner::onReadyReadStderr() {
    QByteArray data = m_process->readAllStandardError();
    QString chunk = QString::fromLocal8Bit(data);
    m_accumulatedOutput += chunk;
    const auto lines = chunk.split('\n', Qt::SkipEmptyParts);
    for (const auto &line : lines) {
        emit logLine("[stderr] " + line);
        if (line.contains("Wrong flash encryption key", Qt::CaseInsensitive) ||
            line.contains("Flash encryption key mismatch", Qt::CaseInsensitive)) {
            emit wrongKeyDetected();
        }
    }
}

void EsptoolRunner::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    bool success = (status == QProcess::NormalExit && exitCode == 0);

    switch (m_currentOp) {
    case Op::ChipInfo:
        emit chipInfoReady(m_accumulatedOutput);
        break;
    case Op::DetectEncryption: {
        bool fusesBurned = m_accumulatedOutput.contains("FLASH_CRYPT_CNT") ||
                           m_accumulatedOutput.contains("SPI_BOOT_CRYPT_CNT");
        bool encrypted = false;
        // crude heuristic -- refine per chip family once you lock down target(s)
        static QRegularExpression re(R"(CRYPT_CNT\s*=\s*0x?0*([1-9a-fA-F][0-9a-fA-F]*))");
        encrypted = re.match(m_accumulatedOutput).hasMatch();
        emit encryptionStateDetected(encrypted, fusesBurned);
        break;
    }
    default:
        break;
    }

    emit finished(success, success ? "Completed successfully." : QString("Exited with code %1").arg(exitCode));
    m_currentOp = Op::None;
}