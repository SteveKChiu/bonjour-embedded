INCLUDEPATH += \
    $$PWD

!macx {
    LIBS += -L$$OUT_PWD/../../bin
    LIBS += -lbonjour
}
