QT += widgets
CONFIG += c++17

TARGET = ESP32FlashTool

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    serialmonitor.cpp

HEADERS += \
    mainwindow.h \
    serialmonitor.h

win32 {
    LIBS += -lsetupapi -ladvapi32 -lole32
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target