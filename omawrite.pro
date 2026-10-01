QT += core gui widgets printsupport qml quick quickcontrols2 quickdialogs2

linux: QT += dbus

CONFIG += c++17 release
TARGET = omawrite
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/markdownhighlighter.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/markdownhighlighter.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc

macos {
    ICON = macos/omawrite.icns
    QMAKE_TARGET_BUNDLE_PREFIX = io.omacom
    QMAKE_BUNDLE_DISPLAY_NAME = Omawrite
    QMAKE_APPLICATION_BUNDLE_NAME = Omawrite
    QMAKE_INFO_PLIST = macos/Info.plist
}

win32 {
    RC_ICONS = windows/omawrite.ico
    QMAKE_TARGET_COMPANY = Omacom
    QMAKE_TARGET_PRODUCT = Omawrite
    QMAKE_TARGET_DESCRIPTION = Omawrite Markdown writing app
}
