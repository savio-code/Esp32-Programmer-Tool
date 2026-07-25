#pragma once
#include <QString>
#include <QObject>

// Handles generation/validation of AES-256 flash encryption keys (32 raw bytes),
// the format esptool/espsecure expects for ESP32 flash encryption.
class KeyManager : public QObject {
    Q_OBJECT
public:
    explicit KeyManager(QObject *parent = nullptr);

    bool generateKey(const QString &path, QString *errorOut = nullptr);
    bool isValidKeyFile(const QString &path, QString *errorOut = nullptr) const;
};