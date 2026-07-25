#include "KeyManager.h"
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

KeyManager::KeyManager(QObject *parent) : QObject(parent) {}

bool KeyManager::generateKey(const QString &path, QString *errorOut) {
    QByteArray key;
    key.resize(32);

    quint32 *buf = reinterpret_cast<quint32 *>(key.data());
    for (int i = 0; i < 8; ++i) {
        buf[i] = QRandomGenerator::system()->generate();
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    f.write(key);
    f.close();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool KeyManager::isValidKeyFile(const QString &path, QString *errorOut) const {
    QFileInfo fi(path);
    if (!fi.exists()) {
        if (errorOut) *errorOut = "Key file does not exist.";
        return false;
    }
    if (fi.size() != 32) {
        if (errorOut) *errorOut = QString("Key file must be exactly 32 bytes (found %1).").arg(fi.size());
        return false;
    }
    return true;
}