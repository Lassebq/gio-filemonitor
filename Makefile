SOURCE = main.c
CC = gcc
CFLAGS = `pkg-config --libs --cflags gio-unix-2.0` -Wall
ifeq ($(USE_LIBNOTIFY), 1)
CFLAGS += -DLIBNOTIFY_DISABLE_DEPRECATED -DUSE_LIBNOTIFY `pkg-config --libs --cflags libnotify`
endif
OUT = ./gio-filemonitor

release:
	$(CC) -O2 $(CFLAGS) $(SOURCE) -o $(OUT)

debug:
	$(CC) -O0 -g $(CFLAGS) $(SOURCE) -o $(OUT)