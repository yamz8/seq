QT += core gui qml quick quickcontrols2 testlib dbus concurrent svg
CONFIG += c++17 testcase
TARGET = seqview_tests
TEMPLATE = app

LIBS += -lz
INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/chart.h ../src/exporter.h ../src/fastx.h ../src/filepicker.h \
    ../src/models.h ../src/portalfilepicker.h ../src/seqtools.h ../src/theme.h ../src/translator.h
SOURCES += seqview_tests.cpp ../src/backend.cpp ../src/chart.cpp ../src/exporter.cpp ../src/fastx.cpp \
    ../src/models.cpp ../src/portalfilepicker.cpp ../src/seqtools.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
