QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets network

CONFIG += c++17

CODECFORSRC = UTF-8

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    TcpPacket.cpp \
    dirtreeview.cpp \
    downloadfilework.cpp \
    filetreeview.cpp \
    main.cpp \
    maindialog.cpp \
    mainwindow.cpp \
    playcore.cpp \
    playerdialog.cpp \
    remotecontroldialog.cpp \
    remotewatchwidget.cpp \
    remotewatchwork.cpp \
    tcpmgr.cpp \
    uploadfilework.cpp

HEADERS += \
    TcpPacket.h \
    dirtreeview.h \
    downloadfilework.h \
    filetreeview.h \
    global.h \
    maindialog.h \
    mainwindow.h \
    playcore.h \
    playerdialog.h \
    remotecontroldialog.h \
    remotewatchwidget.h \
    remotewatchwork.h \
    singleton.hpp \
    tcpmgr.h \
    uploadfilework.h

FORMS += \
    maindialog.ui \
    mainwindow.ui \
    playerdialog.ui \
    remotecontroldialog.ui \
    remotewatchwidget.ui

RESOURCES += \
    resources.qrc

# ==================== libVLC 播放引擎链接配置 ====================
# 通过 qmake "VLC_PATH=C:/path/to/vlc" 或在本文件中配置 VLC SDK 路径。
# 若不指定 VLC_PATH 或路径无效，则不链接 libVLC 并定义 NO_VLC 宏，
# 播放引擎编译为空实现(界面可运行，点击播放时给出提示)，便于无VLC环境下先验证登录/文件管理流程。
# 运行时需将 libvlc.dll、libvlccore.dll 及 plugins/ 目录复制到可执行文件同级目录。

isEmpty(VLC_PATH) {
    VLC_PATH = $$PWD/vlc_sdk
}

VLC_INCLUDE_PATH = $$VLC_PATH/sdk/include
VLC_LIB_PATH     = $$VLC_PATH/sdk/lib

exists($$VLC_INCLUDE_PATH/vlc/vlc.h) {
    INCLUDEPATH += $$VLC_INCLUDE_PATH
    LIBS += -L$$VLC_LIB_PATH -lvlc -lvlccore
    message("libVLC found: $$VLC_PATH")

    # 编译链接后自动将 64位 libvlc.dll 和 libvlccore.dll 拷贝到当前构建输出目录（如 debug/ 或 release/）
    win32 {
        VLC_DLL_SRC = $$replace(VLC_LIB_PATH, /, \\)
        CONFIG(debug, debug|release) {
            VLC_DLL_DST = $$replace(OUT_PWD, /, \\)\\debug
        } else {
            VLC_DLL_DST = $$replace(OUT_PWD, /, \\)\\release
        }
        QMAKE_POST_LINK += copy /Y \"$$VLC_DLL_SRC\\libvlc.dll\" \"$$VLC_DLL_DST\\\" $$escape_expand(\\n\\t)
        QMAKE_POST_LINK += copy /Y \"$$VLC_DLL_SRC\\libvlccore.dll\" \"$$VLC_DLL_DST\\\"
    }
} else {
    DEFINES += NO_VLC
    warning("libVLC SDK not found at $$VLC_PATH, building with NO_VLC (playback disabled).")
}


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
