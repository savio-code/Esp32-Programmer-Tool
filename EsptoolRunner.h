#ifndef ESPTOOLRUNNER_H
#define ESPTOOLRUNNER_H

#include <QObject>
#include <QProcess>
#include <QString>

class EsptoolRunner : public QObject {
    Q_OBJECT

public:
    explicit EsptoolRunner(QObject *parent = nullptr);
    ~EsptoolRunner();

    void flashFirmware(const QString &port, const QString &baudRate, const QString &chip, const QString &binPath, const QString &offset);
    void burnKey(const QString &port, const QString &keyType, const QString &keyFile, const QString &blockName);
    void burnEfuse(const QString &port, const QString &efuseName, const QString &value);
    void resetPort(const QString &port);

signals:
    void outputReceived(const QString &text);
    void errorOccurred(const QString &errorText);
    void processFinished(bool success, int exitCode);

private slots:
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess *m_process;
};

#endif // ESPTOOLRUNNER_H