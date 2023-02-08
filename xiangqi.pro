TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        game.c \
        main.c \
        misc.c \
        solver.c \
        tier.c \
        tiersolver.c \
        tiertree.c

HEADERS += \
    game.h \
    misc.h \
    solver.h \
    tier.h \
    tiersolver.h \
    tiertree.h \
    types.h
