#pragma once

#include <QThread>
#include <QString>
#include <QSet>

// A lightweight stand-in for QSerialPortInfo, since the SerialPort module
// isn't available in this Qt build. Carries just enough info for the GUI.
struct SimplePortInfo
{
    QString portName;      // e.g. "COM5"
    QString description;   // friendly name, e.g. "Silicon Labs CP210x USB to UART Bridge"
    quint16 vendorIdentifier = 0;
    quint16 productIdentifier = 0;
    bool hasVendorIdentifier = false;
    bool hasProductIdentifier = false;
};

// Polls available serial ports (via the Windows registry + SetupAPI) on a
// background thread and emits portDetected/portRemoved when the set of
// ports changes.
class SerialMonitor : public QThread
{
    Q_OBJECT

public:
    explicit SerialMonitor(QObject *parent = nullptr);
    void stop();

    // One-shot enumeration, usable without starting the thread.
    static QList<SimplePortInfo> listPortsNow();

signals:
    void portDetected(const SimplePortInfo &port);
    void portRemoved(const QString &portName);

protected:
    void run() override;

private:
    volatile bool running;
    QSet<QString> knownPorts;
};