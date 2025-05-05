CC = gcc
CFLAGS = -O2 -Wl,-z,norelro -Wl,-z,now -DNDEBUG -fstrict-aliasing -flto -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer -ffast-math -fvisibility=hidden -fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1 -s `pkg-config --cflags --libs gtk+-2.0` -Wno-deprecated-declarations
LDFLAGS = `pkg-config --libs gtk+-2.0`

ICON_PATH = ./icons

all: convert_icons diskactivity_gtk2

disk_icons.h: convert_images.py
	python3 convert_images.py $(ICON_PATH)

diskactivity_gtk2: diskactivity_gtk2.c disk_icons.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

convert_icons: convert_images.py
	python3 convert_images.py $(ICON_PATH)

clean:
	rm -f diskactivity_gtk2 disk_icons.h

.PHONY: all clean convert_icons
