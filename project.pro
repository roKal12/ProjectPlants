TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle

QT += core sql

SOURCES += main.cpp

INCLUDEPATH += C:/vcpkg/installed/x64-mingw-static/include
LIBS += -LC:/vcpkg/installed/x64-mingw-static/lib \
    -lTgBot \
    -lssl \
    -lcrypto \
    -lcurl \
    -lws2_32 \
    -lcrypt32 \
    -ladvapi32 \
    -lboost_system-gcc13-mt-x64-1_88
