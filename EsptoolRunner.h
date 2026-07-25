#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Wraps esptool.py / espefuse.py invocations via QProcess.
// Assumes python + esptool are on PATH (adjust setPythonPath/setEsptoolPath as needed).
class EsptoolRunner : public QObject {
    Q_OBJECT
public:
    explicit EsptoolRunner(QObject *parent = nullptr);

    void setPythonPath(const QString &path);   // e.g. "python" or full path to python.exe
    void setEsptoolPath(const QString &path);  // "esptool" module invocation, e.g. "-m esptool"
    void setPort(const QString &port);

    void readChipInfo();
    void flashImage(const QString &imagePath, quint32 offset, bool encrypt);
    void burnEncryptionFuses();
    void detectEncryptionState();
    void abort();

    bool isRunning() const;

signals:
    void logLine(const QString &line);
    void finished(bool success, const QString &summary);
    void chipInfoReady(const QString &rawOutput);
    void encryptionStateDetected(bool encrypted, bool fusesBurned);
    void wrongKeyDetected();
    void progress(int percent);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startProcess(const QStringList &args);

    QProcess *m_process;
    QString m_pythonPath = "python";
    QString m_port;
    QString m_accumulatedOutput;
    enum class Op { None, ChipInfo, Flash, BurnFuses, DetectEncryption } m_currentOp = Op::None;
};