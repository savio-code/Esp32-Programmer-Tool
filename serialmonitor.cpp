#include "serialmonitor.h"
#include <QRegularExpression>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#endif

SerialMonitor::SerialMonitor(QObject *parent)
    : QThread(parent)
    , running(false)
{
}

void SerialMonitor::stop()
{
    running = false;
}


QList<SimplePortInfo> SerialMonitor::listPortsNow()
{
    QList<SimplePortInfo> result;

#ifdef Q_OS_WIN
    // Step 1: get COMx names from HKLM\HARDWARE\DEVICEMAP\SERIALCOMM
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DEVICEMAP\\SERIALCOMM",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return result;
    }

    DWORD index = 0;
    wchar_t valueData[256];
    for (;;) {
        wchar_t valueName[16383];
        DWORD valueNameSize = 16383;
        DWORD valueDataSize = sizeof(valueData);
        DWORD type = 0;

        LONG enumRes = RegEnumValueW(hKey, index, valueName, &valueNameSize,
                                     nullptr, &type,
                                     reinterpret_cast<LPBYTE>(valueData),
                                     &valueDataSize);
        if (enumRes == ERROR_NO_MORE_ITEMS) break;
        if (enumRes == ERROR_SUCCESS && type == REG_SZ) {
            SimplePortInfo info;
            info.portName = QString::fromWCharArray(valueData);
            result.append(info);
        }
        ++index;
    }
    RegCloseKey(hKey);

    // Step 2: enrich with friendly names + VID/PID from SetupAPI (Ports class)
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr,
                                            DIGCF_PRESENT);
    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devData;
        devData.cbSize = sizeof(SP_DEVINFO_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
            wchar_t friendlyName[512] = {0};
            wchar_t hardwareId[512] = {0};

            SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_FRIENDLYNAME,
                                              nullptr, reinterpret_cast<PBYTE>(friendlyName),
                                              sizeof(friendlyName), nullptr);
            SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_HARDWAREID,
                                              nullptr, reinterpret_cast<PBYTE>(hardwareId),
                                              sizeof(hardwareId), nullptr);

            QString friendly = QString::fromWCharArray(friendlyName);
            QString hwId = QString::fromWCharArray(hardwareId);

            // Friendly name typically looks like "Silicon Labs CP210x ... (COM5)"
            int openParen = friendly.lastIndexOf('(');
            int closeParen = friendly.lastIndexOf(')');
            if (openParen < 0 || closeParen < 0) continue;
            QString comName = friendly.mid(openParen + 1, closeParen - openParen - 1);

            for (auto &info : result) {
                if (info.portName.compare(comName, Qt::CaseInsensitive) == 0) {
                    info.description = friendly.left(openParen).trimmed();

                    // hwId looks like "USB\VID_10C4&PID_EA60\..."
                    QRegularExpression vidRe("VID_([0-9A-Fa-f]{4})");
                    QRegularExpression pidRe("PID_([0-9A-Fa-f]{4})");
                    auto vidMatch = vidRe.match(hwId);
                    auto pidMatch = pidRe.match(hwId);
                    if (vidMatch.hasMatch()) {
                        info.vendorIdentifier = vidMatch.captured(1).toUShort(nullptr, 16);
                        info.hasVendorIdentifier = true;
                    }
                    if (pidMatch.hasMatch()) {
                        info.productIdentifier = pidMatch.captured(1).toUShort(nullptr, 16);
                        info.hasProductIdentifier = true;
                    }
                    break;
                }
            }
        }
        SetupDiDestroyDeviceInfoList(devInfo);
    }

    std::sort(result.begin(), result.end(), [](const SimplePortInfo &a, const SimplePortInfo &b) {
        auto numOf = [](const QString &s) -> int {
            QString digits;
            for (QChar c : s) if (c.isDigit()) digits += c;
            return digits.isEmpty() ? 0 : digits.toInt();
        };
        return numOf(a.portName) < numOf(b.portName);
    });
#endif

    return result;
}

void SerialMonitor::run()
{
    running = true;

    while (running) {
        const auto ports = listPortsNow();
        QSet<QString> currentPorts;

        for (const auto &info : ports) {
            currentPorts.insert(info.portName);
            if (!knownPorts.contains(info.portName)) {
                emit portDetected(info);
            }
        }

        for (const QString &known : std::as_const(knownPorts)) {
            if (!currentPorts.contains(known)) {
                emit portRemoved(known);
            }
        }

        knownPorts = currentPorts;
        QThread::msleep(1500);
    }
}