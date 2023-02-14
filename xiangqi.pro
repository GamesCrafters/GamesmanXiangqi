TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        frontier.c \
        game.c \
        main.c \
        misc.c \
        solver.c \
        tests/tier_test.c \
        tier.c \
        tiersolver.c \
        tiertree.c

HEADERS += \
    frontier.h \
    game.h \
    misc.h \
    solver.h \
    tests/tier_test.h \
    tier.h \
    tiersolver.h \
    tiertree.h \
    types.h
