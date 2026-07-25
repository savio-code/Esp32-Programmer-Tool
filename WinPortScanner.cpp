#include "WinPortScanner.h"
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

WinPortScanner::WinPortScanner(QObject *parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &WinPortScanner::poll);
}

void WinPortScanner::start(int intervalMs) {
    m_timer.start(intervalMs);
    poll();
}

void WinPortScanner::stop() {
    m_timer.stop();
}

QStringList WinPortScanner::listPortsNow() {
    QStringList result;
#ifdef Q_OS_WIN
    HKEY hKey;
    LONG res = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                             L"HARDWARE\\DEVICEMAP\\SERIALCOMM",
                             0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) {
        return result;
    }

    DWORD index = 0;
    wchar_t valueName[16383];
    wchar_t valueData[256];

    for (;;) {
        DWORD valueNameSize = 16383;
        DWORD valueDataSize = sizeof(valueData);
        DWORD type = 0;

        LONG enumRes = RegEnumValueW(hKey, index, valueName, &valueNameSize,
                                     nullptr, &type,
                                     reinterpret_cast<LPBYTE>(valueData),
                                     &valueDataSize);
        if (enumRes == ERROR_NO_MORE_ITEMS) break;
        if (enumRes == ERROR_SUCCESS && type == REG_SZ) {
            result << QString::fromWCharArray(valueData);
        }
        ++index;
    }

    RegCloseKey(hKey);

    std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        auto numOf = [](const QString &s) -> int {
            QString digits;
            for (QChar c : s) if (c.isDigit()) digits += c;
            return digits.isEmpty() ? 0 : digits.toInt();
        };
        return numOf(a) < numOf(b);
    });
#endif
    return result;
}

void WinPortScanner::poll() {
    QStringList current = listPortsNow();
    if (current != m_lastPorts) {
        m_lastPorts = current;
        emit portsChanged(current);
    }
}