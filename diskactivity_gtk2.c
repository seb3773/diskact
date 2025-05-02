#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#define ICON_ACTIVITY "/usr/share/pixmaps/driveact.png"
#define ICON_NO_ACTIVITY "/usr/share/pixmaps/drivenoact.png"
#define ICON_WINDOW "/usr/share/pixmaps/diskact.png"
#define WINDOW_WIDTH 165
#define WINDOW_HEIGHT 180
#define CHECK_INTERVAL 400
#define UPDATE_INTERVAL 1000

typedef struct {
    GtkStatusIcon *status_icon;
    unsigned long prev_sectors_read;
    unsigned long prev_sectors_written;
    gboolean current_state;
    GtkWidget *window;
    GtkWidget *dirty_label;
    GtkWidget *writecache_label;
    guint update_timeout;
} DiskData;

static void on_quit_activate(GtkMenuItem *item, gpointer user_data);
static void on_status_icon_popup(GtkStatusIcon *status_icon, guint button,
                                 guint activate_time, gpointer user_data);
gboolean check_disk_activity(gpointer user_data);
gboolean update_dirty_writecache(gpointer user_data);
void show_dirty_writecache_window(GtkMenuItem *item, gpointer user_data);
static void on_close_button_clicked(GtkWidget *widget, gpointer user_data);
static void on_sync_button_clicked(GtkWidget *widget, gpointer user_data);

char* format_with_thousands_separator(unsigned long value) {
    static char formatted[32];
    char temp[32];
    int i, j, k;
    snprintf(temp, sizeof(temp), "%lu", value);
    int len = strlen(temp);
    for (i = 0, j = 0, k = len % 3 ? len % 3 : 3; i < len; i++, k--) {
        formatted[j++] = temp[i];
        if (k == 0 && i < len - 1) {
            formatted[j++] = ' ';
            k = 3;
        }
    }
    formatted[j] = '\0';
    return formatted;
}


gboolean check_disk_activity(gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    FILE *fp = fopen("/proc/diskstats", "r");
    if (!fp) return TRUE;
    char line[128];
    unsigned long total_sectors_read = 0, total_sectors_written = 0;
    while (fgets(line, sizeof(line), fp)) {
        unsigned int major, minor;
        char dev_name[16];
        unsigned long rsect, wsect;
        if (sscanf(line, "%u %u %15s %*u %*u %lu %*u %*u %*u %lu",
                   &major, &minor, dev_name, &rsect, &wsect) == 5) {
            if (
                ((strncmp(dev_name, "sd", 2) == 0 || strncmp(dev_name, "hd", 2) == 0) && minor % 16 == 0) ||
                (strncmp(dev_name, "nvme", 4) == 0 && strstr(dev_name, "p") == NULL) ||
                (strncmp(dev_name, "mmcblk", 6) == 0 && strstr(dev_name, "p") == NULL) ||
                (strncmp(dev_name, "md", 2) == 0) ||
                (strncmp(dev_name, "pmem", 4) == 0 && strstr(dev_name, "p") == NULL)
            ) {
                total_sectors_read += rsect;
                total_sectors_written += wsect;
            }
        }
    }
    fclose(fp);
    gboolean activity = (total_sectors_read > disk_data->prev_sectors_read ||
                         total_sectors_written > disk_data->prev_sectors_written);
    if (activity != disk_data->current_state) {
        disk_data->current_state = activity;
        gtk_status_icon_set_from_file(disk_data->status_icon,
                                      activity ? ICON_ACTIVITY : ICON_NO_ACTIVITY);
    }
    disk_data->prev_sectors_read = total_sectors_read;
    disk_data->prev_sectors_written = total_sectors_written;
    return TRUE;
}

static void on_quit_activate(GtkMenuItem *item, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    if (disk_data->window) {
        gtk_widget_destroy(disk_data->window);
    }
    g_slice_free(DiskData, user_data);
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


gboolean update_dirty_writecache(gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return FALSE;
    char line[128];
    unsigned long dirty = 0, writeback = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "Dirty: %lu", &dirty) == 1) {
            continue;
        }
        if (sscanf(line, "Writeback: %lu", &writeback) == 1) {
            continue;
        }
    }
    fclose(fp);
    char dirty_text[64], writecache_text[64];
    snprintf(dirty_text, sizeof(dirty_text), "%lu", dirty);
    snprintf(writecache_text, sizeof(writecache_text), "%lu", writeback);
    gtk_label_set_text(GTK_LABEL(disk_data->dirty_label), dirty_text);
    gtk_label_set_text(GTK_LABEL(disk_data->writecache_label), writecache_text);
    return TRUE;
}


static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    if (disk_data->update_timeout) {
        g_source_remove(disk_data->update_timeout);
        disk_data->update_timeout = 0;
    }
    disk_data->window = NULL;
}


static void on_close_button_clicked(GtkWidget *widget, gpointer user_data) {
    DiskData *disk_data = (DiskData *)user_data;
    gtk_widget_destroy(disk_data->window);
}

static void on_sync_button_clicked(GtkWidget *widget, gpointer user_data) {
    system("sync");
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
        gtk_window_set_decorated(GTK_WINDOW(disk_data->window), FALSE);
        gtk_window_set_keep_above(GTK_WINDOW(disk_data->window), TRUE);
        gtk_window_set_icon_from_file(GTK_WINDOW(disk_data->window), ICON_WINDOW, NULL);
        GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
        gtk_container_add(GTK_CONTAINER(disk_data->window), vbox);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        GtkWidget *dirty_frame = gtk_frame_new("Dirty");
        GtkWidget *writecache_frame = gtk_frame_new("Writeback");
        disk_data->dirty_label = gtk_label_new("");
        disk_data->writecache_label = gtk_label_new(" ");
        gtk_widget_set_size_request(dirty_frame, WINDOW_WIDTH - 20, -1); // -20 pour les marges
        gtk_widget_set_size_request(writecache_frame, WINDOW_WIDTH - 20, -1);
        gtk_misc_set_alignment(GTK_MISC(disk_data->dirty_label), 0.5, 0.5);
        gtk_misc_set_alignment(GTK_MISC(disk_data->writecache_label), 0.5, 0.5);
        PangoAttrList *attr_list = pango_attr_list_new();
        PangoAttribute *attr_weight = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        PangoAttribute *attr_size = pango_attr_size_new(16 * PANGO_SCALE);
        pango_attr_list_insert(attr_list, attr_weight);
        pango_attr_list_insert(attr_list, attr_size);
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
    if (access(ICON_ACTIVITY, F_OK) == -1 || access(ICON_NO_ACTIVITY, F_OK) == -1) {
        fprintf(stderr, "icons missing\n");
        return 1;
    }
    DiskData *disk_data = g_slice_new0(DiskData);
    disk_data->prev_sectors_read = 0;
    disk_data->prev_sectors_written = 0;
    disk_data->current_state = FALSE;
    disk_data->window = NULL;
    disk_data->status_icon = gtk_status_icon_new();
    gtk_status_icon_set_from_file(disk_data->status_icon, ICON_NO_ACTIVITY);
    gtk_status_icon_set_tooltip(disk_data->status_icon, "Disk activity");
    gtk_status_icon_set_visible(disk_data->status_icon, TRUE);
    g_signal_connect(G_OBJECT(disk_data->status_icon), "popup-menu",
                     G_CALLBACK(on_status_icon_popup), disk_data);
    g_signal_connect(G_OBJECT(disk_data->status_icon), "activate",
                     G_CALLBACK(show_dirty_writecache_window), disk_data);
    g_timeout_add(CHECK_INTERVAL, check_disk_activity, disk_data);
    gtk_main();
    return 0;
}

