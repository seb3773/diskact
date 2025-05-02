DiskActivity
------------
DiskActivity is a lightweight system tray application for Linux that monitors disk activity and displays real-time information about dirty and writeback memory.

You can build GTK3 or GTK2 version. The reason there's a GTK2 version,although GTK2 is really outdated, is because it uses much less memory than the gtk3 version,
 and I don't need using advanced theming features for this kind of application. GTK3 is provided if you don't want to use GTK2 :)

This app was originally written for my q4rescue live system project based on q4os trinity: https://sourceforge.net/projects/q4rescue/


~~ Features:
- System Tray Icon: Displays disk activity status with active/inactive state icon.
- Real-Time Monitoring: Tracks disk read/write operations using /proc/diskstats.
- Memory Information: Shows dirty and writeback memory values from /proc/meminfo in a dedicated window.



~~ User Interaction:

- Left-click the tray icon to open or show the information window
- Right-click to access a context menu with options to show the window or quit the application.
- Sync Support: Includes a "Sync" button to trigger the sync command for flushing disk buffers.




--> Compiling binary:

gtk3:

gcc -O2  -fstrict-aliasing -flto -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer -ffast-math -fvisibility=hidden -fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1 -s -o diskactivity_gtk3 diskactivity_gtk3.c `pkg-config --cflags --libs gtk+-3.0`

gtk2:

gcc -O2 -fstrict-aliasing -flto -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer -ffast-math -fvisibility=hidden -fuse-ld=gold -Wl,--gc-sections,--build-id=none,-O1 -s -o diskactivity_gtk2 diskactivity_gtk2.c `pkg-config --cflags --libs gtk+-2.0`

Don't forget to copy the icons (driveact.png, drivenoact.png and diskact.png) in the folder /usr/share/pixmaps/  (create it if it doesn't exist).

** Licence : WTFYW     ;-)
