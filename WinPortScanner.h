#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>

// Enumerates COM ports on Windows via the registry -- no Qt SerialPort module needed.
class WinPortScanner : public QObject {
    Q_OBJECT
public:
    explicit WinPortScanner(QObject *parent = nullptr);
    void start(int intervalMs = 1500);
    void stop();

    static QStringList listPortsNow();

signals:
    void portsChanged(const QStringList &ports);

private slots:
    void poll();

private:
    QTimer m_timer;
    QStringList m_lastPorts;
};