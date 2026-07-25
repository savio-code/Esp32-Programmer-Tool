# 🔐 ESP32 Flasher & Encryption Manager

<div align="center">

![ESP32 Flasher Logo](images/logo.png)

**A comprehensive desktop application for programming ESP32 devices with hardware-based flash encryption**

[![Version](https://img.shields.io/badge/version-2.0.0-blue.svg)](https://github.com/savio-code/esp32-flasher)
[![Qt](https://img.shields.io/badge/Qt-6.11.0-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

</div>

---

## 📋 Overview

**ESP32 Flasher & Encryption Manager** is a comprehensive desktop application that simplifies the process of programming ESP32 devices while providing robust firmware protection through hardware-based flash encryption. Whether you're a hobbyist developing IoT devices or a professional deploying products in the field, this tool gives you complete control over your ESP32's security.

![Main Interface](images/main_interface.png)

### ✨ Features at a Glance

- 🔐 **Hardware-accelerated flash encryption** - Protect your firmware at the hardware level
- 🔑 **User-controlled encryption keys** - You generate and keep your own keys
- 🔒 **Security fuse management** - Permanently disable debug interfaces
- 📦 **Universal flashing** - Bootloader, partition table, and application in one go
- 🔄 **Smart detection** - Automatically detects encryption status
- 🛡️ **Platform independence** - Works with PlatformIO, Arduino, and any `.bin` file

---

## 🚀 Key Capabilities

### 🔐 Secure Firmware Protection

The application leverages the ESP32's built-in hardware encryption engine to protect your intellectual property. By burning a unique encryption key into the chip's **eFuses (one-time programmable memory)**, your firmware becomes permanently encrypted on the device.

![Encryption Process](images/encryption_process.png)

**Even if someone physically reads the flash memory, they cannot extract your source code or proprietary algorithms without the original key.**

### 🔑 User-Controlled Encryption Keys

Unlike systems where keys are auto-generated and stored on the chip (unrecoverable), this tool allows you to **generate and control your own encryption keys**. You keep a copy of the key file on your PC, which means:

- ✅ You can decrypt firmware backups if needed
- ✅ You can re-flash encrypted firmware with confidence
- ✅ You have full ownership of your security

![Key Management](images/key_management.png)

### 🔒 Security Fuse Management

Protect your device from unauthorized access by permanently disabling debug interfaces:

| Fuse | Purpose |
|------|---------|
| **UART Download Mode** | Prevents unauthorized firmware flashing via serial |
| **JTAG Debugging** | Blocks external debugging attempts |
| **UART Flash Encryption** | Additional encryption layer protection |
| **Cache & Console Debug** | Further security hardening |

![Fuse Management](images/fuse_management.png)

> ⚠️ **Warning**: These fuses are **one-time programmable (OTP)**, making them irreversible. Perfect for production-ready devices.

### 📦 Universal Flashing

Supports all standard ESP32 firmware components in one seamless operation:
