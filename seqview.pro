QT += core gui qml quick quickcontrols2 dbus concurrent svg

CONFIG += c++17 release
TARGET = seqview
TEMPLATE = app

LIBS += -lz

HEADERS += \
    src/backend.h \
    src/chart.h \
    src/cli.h \
    src/exporter.h \
    src/fastx.h \
    src/filepicker.h \
    src/models.h \
    src/portalfilepicker.h \
    src/problems.h \
    src/seqtools.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/chart.cpp \
    src/cli.cpp \
    src/exporter.cpp \
    src/fastx.cpp \
    src/models.cpp \
    src/portalfilepicker.cpp \
    src/problems.cpp \
    src/seqtools.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
