TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        common.c \
        frontier.c \
        game.c \
        main.c \
        misc.c \
        solver.c \
        tests/game_test.c \
        tests/tier_test.c \
        tier.c \
        tiersolver.c \
        tiertree.c

HEADERS += \
    common.h \
    frontier.h \
    game.h \
    misc.h \
    solver.h \
    tests/game_test.h \
    tests/tier_test.h \
    tier.h \
    tiersolver.h \
    tiertree.h
