# based on apple bonjour mDNSResponder-765.30.11

TEMPLATE = lib
CONFIG += warn_off

DEFINES += \
    MDNS_DEBUGMSGS=0 \
    MDNS_BUILDINGSTUBLIBRARY \
    USE_TCP_LOOPBACK \
    HAVE_IPV6 \
    PID_FILE=\\\"\\\" \
    PLATFORM_NO_RLIMIT \
    _LEGACY_NAT_TRAVERSAL_

INCLUDEPATH += \
    mDNSCore \
    mDNSShared

SOURCES += \
    mDNSCore/anonymous.c \
    mDNSCore/CryptoAlg.c \
    mDNSCore/DNSCommon.c \
    mDNSCore/DNSDigest.c \
    mDNSCore/mDNS.c \
    mDNSCore/uDNS.c \
    mDNSShared/dnssd_clientlib.c \
    mDNSShared/dnssd_clientshim.c \
    mDNSShared/GenLinkedList.c \
    mDNSShared/mDNSDebug.c \
    mDNSMacOSX/LegacyNATTraversal.c \
    bonjour_main.c \
    bonjour_shim.c

win32 {
    DEFINES += \
        TARGET_OS_WIN32 \
        PLATFORM_NO_STRSEP \
        PLATFORM_NO_EPIPE \
        WIN32 \
        WIN32_LEAN_AND_MEAN \
        UNICODE \
        _WIN32_WINNT=0x0501 \
        _UNICODE \
        _CRT_SECURE_NO_DEPRECATE \
        _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES

    INCLUDEPATH += \
        mDNSWindows \
        mDNSWindows/SystemService

    SOURCES += \
        mDNSWindows/mDNSWin32.c \
        mDNSWindows/Poll.c \
        mDNSWindows/Secret.c \
        mDNSWindows/SystemService/Firewall.cpp

    LIBS += \
        -ladvapi32 \
        -lws2_32 \
        -lole32 \
        -loleaut32 \
        -liphlpapi \
        -lnetapi32

    QMAKE_LFLAGS += \
        /DEF:$$PWD/bonjour.def
}

else:!macx {
    DEFINES += \
        TARGET_OS_LINUX \
        HAVE_LINUX \
        USES_NETLINK \
        NOT_HAVE_SA_LEN \
        _SS_MAXSIZE=_K_SS_MAXSIZE

    INCLUDEPATH += \
        mDNSPosix

    SOURCES += \
        mDNSPosix/mDNSPosix.c \
        mDNSPosix/mDNSUNP.c

    QMAKE_LFLAGS += \
        -Wl,--version-script=$$PWD/bonjour.lds
}
