#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "disk_icons.h"

#define WINDOW_WIDTH 165
#define WINDOW_HEIGHT 180
#define CHECK_INTERVAL 400
#define UPDATE_INTERVAL 1000

typedef struct {
    GtkStatusIcon *status_icon;
    uint32_t prev_sectors_read;
    uint32_t prev_sectors_written;
    gboolean current_state;
    GtkWidget *window;
    GtkWidget *dirty_label;
    GtkWidget *writecache_label;
    guint update_timeout;
} DiskData;

static GdkPixbuf *pixbuf_activity = NULL;
static GdkPixbuf *pixbuf_no_activity = NULL;
static GdkPixbuf *pixbuf_window = NULL;
static DiskData disk_data = {0};
static char diskstats_buf[4096];
static char meminfo_buf[512];

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define STR_EQ2(a,b) (*(uint16_t*)(a)==*(uint16_t*)(b))

static inline GdkPixbuf* pixbuf_from_data(const unsigned char *data, size_t size) {
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, data, size, NULL);
    gdk_pixbuf_loader_close(loader, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pixbuf) g_object_ref(pixbuf);
    g_object_unref(loader);
    return pixbuf;
}

static inline void parse_diskstats(uint32_t *rsect, uint32_t *wsect) {
    int fd = open("/proc/diskstats", O_RDONLY);
    *rsect = 0; *wsect = 0;
    if (unlikely(fd < 0)) return;
    ssize_t len = read(fd, diskstats_buf, sizeof(diskstats_buf)-1);
    close(fd);
    if (unlikely(len <= 0)) return;
    diskstats_buf[len] = 0;
    char *p = diskstats_buf;
    while (*p) {
        while (*p == ' ') ++p;
        unsigned int major = 0;
        while (*p >= '0' && *p <= '9') major = major*10 + (*p++ - '0');
        while (*p == ' ') ++p;
        unsigned int minor = 0;
        while (*p >= '0' && *p <= '9') minor = minor*10 + (*p++ - '0');
        while (*p == ' ') ++p;
        char dev[12]; int i=0;
        while (*p && *p != ' ' && i<11) dev[i++] = *p++;
        dev[i]=0;
        while (*p == ' ') ++p;
        for (int j=0;j<2;++j) { while (*p && *p!=' ') ++p; while (*p==' ') ++p; }
        uint32_t r = 0;
        while (*p >= '0' && *p <= '9') r = r*10 + (*p++ - '0');
        while (*p == ' ') ++p;
        while (*p && *p!=' ') ++p; while (*p==' ') ++p;
        uint32_t w = 0;
        while (*p >= '0' && *p <= '9') w = w*10 + (*p++ - '0');
        if ((dev[0]=='s'&&dev[1]=='d'&&minor%16==0) ||
            (dev[0]=='h'&&dev[1]=='d'&&minor%64==0) ||
            (!strncmp(dev,"nvme",4)&&!strstr(dev,"p")) ||
            (!strncmp(dev,"mmcblk",6)&&!strstr(dev,"p")) ||
            (dev[0]=='m'&&dev[1]=='d') ||
            (!strncmp(dev,"pmem",4)&&!strstr(dev,"p"))) {
            *rsect += r;
            *wsect += w;
        }
        p = strchr(p, '\n');
        if (!p) break;
        ++p;
    }
}

static inline void parse_dirty_writeback(uint32_t *dirty, uint32_t *writeback) {
    int fd = open("/proc/meminfo", O_RDONLY);
    *dirty=0; *writeback=0;
    if (unlikely(fd < 0)) return;
    ssize_t len = read(fd, meminfo_buf, sizeof(meminfo_buf)-1);
    close(fd);
    if (unlikely(len <= 0)) return;
    meminfo_buf[len]=0;
    char *p = meminfo_buf;
    while (*p) {
        if (!strncmp(p,"Dirty:",6)) *dirty = (uint32_t)atoi(p+6);
        else if (!strncmp(p,"Writeback:",9)) *writeback = (uint32_t)atoi(p+9);
        p = strchr(p,'\n'); if (!p) break; ++p;
    }
}

static void on_quit_activate(GtkMenuItem *item, gpointer user_data);
static void on_status_icon_popup(GtkStatusIcon *status_icon, guint button, guint activate_time, gpointer user_data);
gboolean check_disk_activity(gpointer user_data);
gboolean update_dirty_writecache(gpointer user_data);
void show_dirty_writecache_window(GtkMenuItem *item, gpointer user_data);
static void on_close_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_sync_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_window_destroy(GtkWidget *widget, gpointer user_data);
static void position_window_bottom_right(GtkWidget *window);

gboolean check_disk_activity(gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    uint32_t total_sectors_read = 0, total_sectors_written = 0;
    parse_diskstats(&total_sectors_read, &total_sectors_written);
    gboolean activity = (total_sectors_read > disk_data->prev_sectors_read ||
                         total_sectors_written > disk_data->prev_sectors_written);
    if (activity != disk_data->current_state) {
        disk_data->current_state = activity;
        gtk_status_icon_set_from_pixbuf(disk_data->status_icon,
            activity ? pixbuf_activity : pixbuf_no_activity);
    }
    disk_data->prev_sectors_read = total_sectors_read;
    disk_data->prev_sectors_written = total_sectors_written;
    return TRUE;
}

gboolean update_dirty_writecache(gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return FALSE;
    char line[96];
    uint32_t dirty = 0, writeback = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!dirty && strncmp(line, "Dirty:", 6) == 0) {
            dirty = (uint32_t)strtoul(line + 6, NULL, 10);
        } else if (!writeback && strncmp(line, "Writeback:", 9) == 0) {
            writeback = (uint32_t)strtoul(line + 9, NULL, 10);
        }
        if (dirty && writeback) break;
    }
    fclose(fp);
    char dirty_text[32], writecache_text[32];
    snprintf(dirty_text, sizeof(dirty_text), "%u", dirty);
    snprintf(writecache_text, sizeof(writecache_text), "%u", writeback);
    gtk_label_set_text(GTK_LABEL(disk_data->dirty_label), dirty_text);
    gtk_label_set_text(GTK_LABEL(disk_data->writecache_label), writecache_text);
    return TRUE;
}


static void on_quit_activate(GtkMenuItem *item, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    if (disk_data->window) gtk_widget_destroy(disk_data->window);
    gtk_main_quit();
}

static void on_status_icon_popup(GtkStatusIcon *status_icon, guint button,
                                 guint activate_time, gpointer user_data) {
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    GtkWidget *show_item = gtk_menu_item_new_with_label("Show dirty/writeback");
    g_signal_connect(G_OBJECT(quit_item), "activate", G_CALLBACK(on_quit_activate), user_data);
    g_signal_connect(G_OBJECT(show_item), "activate", G_CALLBACK(show_dirty_writecache_window), user_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), show_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                   gtk_status_icon_position_menu, status_icon,
                   button, activate_time);
}

static void on_close_button_clicked(GtkWidget *widget, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    if (disk_data->window) gtk_widget_destroy(disk_data->window);
}

static void on_sync_button_clicked(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    sync();
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    if (disk_data->update_timeout) {
        g_source_remove(disk_data->update_timeout);
        disk_data->update_timeout = 0;
    }
    disk_data->window = NULL;
}

static void position_window_bottom_right(GtkWidget *window) {
    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;
    GdkScreen *screen = gdk_display_get_screen(display, 0);
    gint screen_width = gdk_screen_get_width(screen);
    gint screen_height = gdk_screen_get_height(screen);
    gint actual_width, actual_height;
    gtk_window_get_size(GTK_WINDOW(window), &actual_width, &actual_height);
    gint x_pos = screen_width - actual_width - 4;
    gint y_pos = screen_height - actual_height - 22;
    if (x_pos < 0) x_pos = 0;
    if (y_pos < 0) y_pos = 0;
    gtk_window_move(GTK_WINDOW(window), x_pos, y_pos);
}

void show_dirty_writecache_window(GtkMenuItem *item, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    if (!disk_data->window) {
        disk_data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(disk_data->window), "");
        gtk_window_set_default_size(GTK_WINDOW(disk_data->window), WINDOW_WIDTH, WINDOW_HEIGHT);
        gtk_window_set_resizable(GTK_WINDOW(disk_data->window), FALSE);
        gtk_window_set_position(GTK_WINDOW(disk_data->window), GTK_WIN_POS_NONE);
        gtk_window_set_decorated(GTK_WINDOW(disk_data->window), TRUE);
        gtk_window_set_keep_above(GTK_WINDOW(disk_data->window), TRUE);
        gtk_window_set_icon(GTK_WINDOW(disk_data->window), pixbuf_window);
        GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_add(GTK_CONTAINER(disk_data->window), vbox);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        GtkWidget *dirty_frame = gtk_frame_new("Dirty");
        GtkWidget *writecache_frame = gtk_frame_new("Writeback");
        disk_data->dirty_label = gtk_label_new("");
        disk_data->writecache_label = gtk_label_new(" ");
        gtk_widget_set_size_request(dirty_frame, WINDOW_WIDTH - 20, -1);
        gtk_widget_set_size_request(writecache_frame, WINDOW_WIDTH - 20, -1);
        gtk_misc_set_alignment(GTK_MISC(disk_data->dirty_label), 0.5, 0.5);
        gtk_misc_set_alignment(GTK_MISC(disk_data->writecache_label), 0.5, 0.5);
        PangoAttrList *attr_list = pango_attr_list_new();
        pango_attr_list_insert(attr_list, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        pango_attr_list_insert(attr_list, pango_attr_size_new(16 * PANGO_SCALE));
        gtk_label_set_attributes(GTK_LABEL(disk_data->dirty_label), attr_list);
        gtk_label_set_attributes(GTK_LABEL(disk_data->writecache_label), attr_list);
        pango_attr_list_unref(attr_list);
        gtk_container_add(GTK_CONTAINER(dirty_frame), disk_data->dirty_label);
        gtk_container_add(GTK_CONTAINER(writecache_frame), disk_data->writecache_label);
        GtkWidget *sync_button = gtk_button_new_with_label("Sync");
        GtkWidget *close_button = gtk_button_new_with_label("Close");
        gtk_widget_set_size_request(sync_button, WINDOW_WIDTH - 20, -1);
        gtk_widget_set_size_request(close_button, WINDOW_WIDTH - 20, -1);
        g_signal_connect(G_OBJECT(sync_button), "clicked", G_CALLBACK(on_sync_button_clicked), NULL);
        g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(on_close_button_clicked), disk_data);
        gtk_box_pack_start(GTK_BOX(vbox), dirty_frame, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), writecache_frame, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), sync_button, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), close_button, TRUE, TRUE, 0);
        g_signal_connect(G_OBJECT(disk_data->window), "destroy", G_CALLBACK(on_window_destroy), disk_data);
        gtk_widget_show_all(disk_data->window);
        position_window_bottom_right(disk_data->window);
        disk_data->update_timeout = g_timeout_add(UPDATE_INTERVAL, update_dirty_writecache, disk_data);
    } else {
        gtk_window_present(GTK_WINDOW(disk_data->window));
        position_window_bottom_right(disk_data->window);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    pixbuf_activity = pixbuf_from_data(driveact_data, driveact_size);
    pixbuf_no_activity = pixbuf_from_data(drivenoact_data, drivenoact_size);
    pixbuf_window = pixbuf_from_data(diskact_data, diskact_size);
    disk_data.prev_sectors_read = 0;
    disk_data.prev_sectors_written = 0;
    disk_data.current_state = FALSE;
    disk_data.window = NULL;
    disk_data.status_icon = gtk_status_icon_new();
    gtk_status_icon_set_from_pixbuf(disk_data.status_icon, pixbuf_no_activity);
    gtk_status_icon_set_tooltip(disk_data.status_icon, "Disk activity");
    gtk_status_icon_set_visible(disk_data.status_icon, TRUE);
    g_signal_connect(G_OBJECT(disk_data.status_icon), "popup-menu",
                     G_CALLBACK(on_status_icon_popup), &disk_data);
    g_signal_connect(G_OBJECT(disk_data.status_icon), "activate",
                     G_CALLBACK(show_dirty_writecache_window), &disk_data);
    g_timeout_add(CHECK_INTERVAL, check_disk_activity, &disk_data);
    gtk_main();
    g_object_unref(pixbuf_activity);
    g_object_unref(pixbuf_no_activity);
    g_object_unref(pixbuf_window);
    return 0;
}
