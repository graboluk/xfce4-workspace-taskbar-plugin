/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libwnck/libwnck.h>

#define WORKSPACE_MIN_WIDTH 92
#define ROOT_SPACING 4
#define WORKSPACE_INNER_SPACING 2
#define DEFAULT_ICON_SIZE 18
#define DEFAULT_LABEL_FONT_SIZE 9
#define DEFAULT_ICON_SPACING 2

typedef struct
{
    XfcePanelPlugin *plugin;
    GtkWidget *root;
    GtkCssProvider *css;

    WnckHandle *wnck_handle;
    WnckScreen *screen;

    GHashTable *watched_windows;
    gint panel_size;
    gint label_font_size;
    gint icon_spacing;

    GtkWidget *swap_menu_item;
    GtkWidget *swap_submenu;
    gint context_workspace_number;
    gboolean swap_in_progress;
} WorkspaceTaskbar;

typedef struct
{
    WorkspaceTaskbar *self;
    gint workspace_number;
} WorkspaceContext;

static void workspace_taskbar_rebuild (WorkspaceTaskbar *self);
static void workspace_taskbar_update_css (WorkspaceTaskbar *self);
static void workspace_taskbar_save (XfcePanelPlugin *plugin,
                                    WorkspaceTaskbar *self);

static guint32
current_event_time (void)
{
    return gtk_get_current_event_time ();
}

static void
unref_signal_data (gpointer data,
                   GClosure *closure)
{
    g_object_unref (data);
}

static void
free_signal_data (gpointer data,
                  GClosure *closure)
{
    g_free (data);
}

static void
destroy_all_children (GtkWidget *container)
{
    GList *children = gtk_container_get_children (GTK_CONTAINER (container));

    for (GList *item = children; item != NULL; item = item->next)
        gtk_widget_destroy (GTK_WIDGET (item->data));

    g_list_free (children);
}

static void
workspace_taskbar_request_rebuild (WorkspaceTaskbar *self)
{
    if (!self->swap_in_progress)
        workspace_taskbar_rebuild (self);
}

static gboolean
window_is_swappable (WnckWindow *window)
{
    WnckWindowType type;

    if (wnck_window_is_pinned (window))
        return FALSE;

    type = wnck_window_get_window_type (window);
    return type != WNCK_WINDOW_DESKTOP && type != WNCK_WINDOW_DOCK;
}

static void
workspace_taskbar_swap_workspaces (WorkspaceTaskbar *self,
                                    gint source_number,
                                    gint target_number)
{
    WnckWorkspace *source;
    WnckWorkspace *target;
    GPtrArray *source_windows;
    GPtrArray *target_windows;
    GList *windows;

    if (source_number == target_number)
        return;

    source = wnck_screen_get_workspace (self->screen, source_number);
    target = wnck_screen_get_workspace (self->screen, target_number);
    if (source == NULL || target == NULL)
        return;

    source_windows = g_ptr_array_new_with_free_func (g_object_unref);
    target_windows = g_ptr_array_new_with_free_func (g_object_unref);

    /* Snapshot both sides before moving anything.  Stacked order gives a
     * deterministic traversal and avoids one move changing membership tests
     * for the second half of the swap. */
    windows = wnck_screen_get_windows_stacked (self->screen);
    for (GList *item = windows; item != NULL; item = item->next)
    {
        WnckWindow *window = WNCK_WINDOW (item->data);
        WnckWorkspace *workspace;

        if (!window_is_swappable (window))
            continue;

        workspace = wnck_window_get_workspace (window);
        if (workspace == source)
            g_ptr_array_add (source_windows, g_object_ref (window));
        else if (workspace == target)
            g_ptr_array_add (target_windows, g_object_ref (window));
    }

    self->swap_in_progress = TRUE;

    for (guint i = 0; i < source_windows->len; ++i)
        wnck_window_move_to_workspace (
            WNCK_WINDOW (g_ptr_array_index (source_windows, i)),
            target);

    for (guint i = 0; i < target_windows->len; ++i)
        wnck_window_move_to_workspace (
            WNCK_WINDOW (g_ptr_array_index (target_windows, i)),
            source);

    /* Pull in changes already processed by the X server while suppressing
     * the intermediate rebuild storm.  Later asynchronous signals are still
     * harmless and will refresh again if necessary. */
    wnck_screen_force_update (self->screen);
    self->swap_in_progress = FALSE;

    g_ptr_array_unref (source_windows);
    g_ptr_array_unref (target_windows);

    workspace_taskbar_rebuild (self);
}

static void
swap_target_activated (GtkMenuItem *item,
                       WorkspaceTaskbar *self)
{
    gint stored_number;
    gint target_number;

    stored_number = GPOINTER_TO_INT (
        g_object_get_data (G_OBJECT (item), "workspace-number"));
    target_number = stored_number - 1;

    if (target_number < 0 || self->context_workspace_number < 0)
        return;

    workspace_taskbar_swap_workspaces (self,
                                       self->context_workspace_number,
                                       target_number);
}

static void
workspace_taskbar_refresh_swap_menu (WorkspaceTaskbar *self)
{
    GList *workspaces;
    gint workspace_count;

    if (self->swap_submenu == NULL || self->swap_menu_item == NULL)
        return;

    destroy_all_children (self->swap_submenu);

    workspace_count = wnck_screen_get_workspace_count (self->screen);
    if (self->context_workspace_number < 0 ||
        self->context_workspace_number >= workspace_count)
    {
        gtk_widget_set_sensitive (self->swap_menu_item, FALSE);
        return;
    }

    gtk_widget_set_sensitive (self->swap_menu_item, workspace_count > 1);

    workspaces = wnck_screen_get_workspaces (self->screen);
    for (GList *entry = workspaces; entry != NULL; entry = entry->next)
    {
        WnckWorkspace *workspace = WNCK_WORKSPACE (entry->data);
        gint number = wnck_workspace_get_number (workspace);
        gchar *label_text = g_strdup_printf ("%d", number + 1);
        GtkWidget *item = gtk_menu_item_new_with_label (label_text);

        g_free (label_text);
        gtk_widget_set_sensitive (item,
                                  number != self->context_workspace_number);
        g_object_set_data (G_OBJECT (item),
                           "workspace-number",
                           GINT_TO_POINTER (number + 1));
        g_signal_connect (item,
                          "activate",
                          G_CALLBACK (swap_target_activated),
                          self);
        gtk_menu_shell_append (GTK_MENU_SHELL (self->swap_submenu), item);
    }

    gtk_widget_show_all (self->swap_submenu);
}

static gboolean
workspace_button_press (GtkWidget *widget,
                        GdkEventButton *event,
                        gpointer user_data)
{
    WorkspaceContext *context = user_data;

    if (event->button != 3)
        return FALSE;

    context->self->context_workspace_number = context->workspace_number;
    workspace_taskbar_refresh_swap_menu (context->self);

    /* Let the Xfce action-widget handler continue and open the normal plugin
     * menu, now with our source-aware Swap with submenu prepared. */
    return FALSE;
}

static gboolean
workspace_button_release (GtkWidget *widget,
                          GdkEventButton *event,
                          gpointer user_data)
{
    WnckWorkspace *workspace = WNCK_WORKSPACE (user_data);

    if (event->button != 1)
        return FALSE;

    wnck_workspace_activate (workspace, event->time);
    return TRUE;
}

static gboolean
window_button_press (GtkWidget *widget,
                     GdkEventButton *event,
                     gpointer user_data)
{
    WnckWindow *window = WNCK_WINDOW (user_data);
    GtkWidget *menu;

    if (event->button != 3)
        return FALSE;

    menu = wnck_action_menu_new (window);
    if (menu == NULL)
        return TRUE;

    gtk_menu_set_screen (GTK_MENU (menu),
                         gtk_widget_get_screen (widget));
    gtk_widget_show (menu);
    gtk_menu_popup_at_widget (GTK_MENU (menu),
                              widget,
                              GDK_GRAVITY_SOUTH_WEST,
                              GDK_GRAVITY_NORTH_WEST,
                              (GdkEvent *) event);
    g_signal_connect (menu,
                      "selection-done",
                      G_CALLBACK (gtk_widget_destroy),
                      NULL);

    return TRUE;
}

static void
window_button_clicked (GtkButton *button,
                       gpointer user_data)
{
    WnckWindow *window = WNCK_WINDOW (user_data);
    WnckWorkspace *workspace;
    guint32 timestamp;

    timestamp = current_event_time ();

    if (wnck_window_is_active (window) && !wnck_window_is_minimized (window))
    {
        wnck_window_minimize (window);
        return;
    }

    workspace = wnck_window_get_workspace (window);
    if (workspace != NULL)
        wnck_workspace_activate (workspace, timestamp);

    if (wnck_window_is_minimized (window))
        wnck_window_unminimize (window, timestamp);

    wnck_window_activate (window, timestamp);
}

static GtkWidget *
window_icon_image (WorkspaceTaskbar *self,
                   WnckWindow *window)
{
    GdkPixbuf *source;
    GdkPixbuf *scaled;
    GtkWidget *image;
    gint icon_size;

    icon_size = DEFAULT_ICON_SIZE;
    if (self->panel_size > 0)
        icon_size = CLAMP (self->panel_size - 10, 16, 22);

    source = wnck_window_get_mini_icon (window);
    if (source == NULL)
        source = wnck_window_get_icon (window);

    if (source == NULL)
        return gtk_image_new_from_icon_name ("application-x-executable-symbolic",
                                             GTK_ICON_SIZE_MENU);

    scaled = gdk_pixbuf_scale_simple (source,
                                      icon_size,
                                      icon_size,
                                      GDK_INTERP_BILINEAR);
    if (scaled == NULL)
        return gtk_image_new_from_pixbuf (source);

    image = gtk_image_new_from_pixbuf (scaled);
    g_object_unref (scaled);
    return image;
}

static GtkWidget *
create_window_button (WorkspaceTaskbar *self,
                      WnckWindow *window)
{
    GtkWidget *button;
    GtkWidget *image;
    GtkStyleContext *style;
    const gchar *title;

    button = gtk_button_new ();
    image = window_icon_image (self, window);
    gtk_container_add (GTK_CONTAINER (button), image);

    gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus (button, FALSE);

    title = wnck_window_get_name (window);
    if (title != NULL)
        gtk_widget_set_tooltip_text (button, title);

    style = gtk_widget_get_style_context (button);
    gtk_style_context_add_class (style, "wt-window-button");

    if (wnck_window_is_active (window))
        gtk_style_context_add_class (style, "active-window");

    if (wnck_window_or_transient_needs_attention (window))
        gtk_style_context_add_class (style, "urgent-window");

    g_signal_connect_data (button,
                           "clicked",
                           G_CALLBACK (window_button_clicked),
                           g_object_ref (window),
                           unref_signal_data,
                           0);
    g_signal_connect_data (button,
                           "button-press-event",
                           G_CALLBACK (window_button_press),
                           g_object_ref (window),
                           unref_signal_data,
                           0);

    return button;
}

static gboolean
window_belongs_to_workspace (WnckWindow *window,
                             WnckWorkspace *workspace)
{
    if (wnck_window_is_skip_tasklist (window))
        return FALSE;

    if (wnck_window_is_pinned (window))
        return TRUE;

    return wnck_window_get_workspace (window) == workspace;
}

static GtkWidget *
create_workspace_box (WorkspaceTaskbar *self,
                      WnckWorkspace *workspace)
{
    GtkWidget *event_box;
    GtkWidget *content;
    GtkWidget *label;
    GtkWidget *window_box;
    GtkStyleContext *style;
    WnckWorkspace *active_workspace;
    GList *windows;
    gchar *number_text;

    event_box = gtk_event_box_new ();
    gtk_event_box_set_visible_window (GTK_EVENT_BOX (event_box), TRUE);
    gtk_widget_set_size_request (event_box, WORKSPACE_MIN_WIDTH, -1);

    style = gtk_widget_get_style_context (event_box);
    gtk_style_context_add_class (style, "wt-workspace");

    active_workspace = wnck_screen_get_active_workspace (self->screen);
    if (workspace == active_workspace)
        gtk_style_context_add_class (style, "active-workspace");

    content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, WORKSPACE_INNER_SPACING);
    gtk_container_add (GTK_CONTAINER (event_box), content);

    number_text = g_strdup_printf ("%d", wnck_workspace_get_number (workspace) + 1);
    label = gtk_label_new (number_text);
    g_free (number_text);

    gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class (gtk_widget_get_style_context (label),
                                 "wt-workspace-number");
    gtk_box_pack_start (GTK_BOX (content), label, FALSE, FALSE, 2);

    /* Keep workspace-label spacing independent from spacing between icons. */
    window_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, self->icon_spacing);
    gtk_box_pack_start (GTK_BOX (content), window_box, TRUE, TRUE, 0);

    windows = wnck_screen_get_windows (self->screen);
    for (GList *item = windows; item != NULL; item = item->next)
    {
        WnckWindow *window = WNCK_WINDOW (item->data);

        if (window_belongs_to_workspace (window, workspace))
        {
            GtkWidget *button = create_window_button (self, window);
            gtk_box_pack_start (GTK_BOX (window_box), button, FALSE, FALSE, 0);
        }
    }

    {
        WorkspaceContext *context = g_new (WorkspaceContext, 1);
        context->self = self;
        context->workspace_number = wnck_workspace_get_number (workspace);

        /* Connect before add_action_widget(), so the source workspace is
         * recorded before Xfce opens the standard plugin context menu. */
        g_signal_connect_data (event_box,
                               "button-press-event",
                               G_CALLBACK (workspace_button_press),
                               context,
                               free_signal_data,
                               0);
    }

    g_signal_connect_data (event_box,
                           "button-release-event",
                           G_CALLBACK (workspace_button_release),
                           g_object_ref (workspace),
                           unref_signal_data,
                           0);

    /* Right-click keeps the normal Xfce plugin menu; our custom Swap with
     * submenu is inserted into that menu at plugin construction time. */
    xfce_panel_plugin_add_action_widget (self->plugin, event_box);

    return event_box;
}

static void
workspace_taskbar_rebuild (WorkspaceTaskbar *self)
{
    GList *workspaces;

    if (self->screen == NULL || self->root == NULL)
        return;

    destroy_all_children (self->root);

    workspaces = wnck_screen_get_workspaces (self->screen);
    for (GList *item = workspaces; item != NULL; item = item->next)
    {
        GtkWidget *workspace_box = create_workspace_box (
            self,
            WNCK_WORKSPACE (item->data));

        gtk_box_pack_start (GTK_BOX (self->root),
                            workspace_box,
                            TRUE,
                            TRUE,
                            0);
    }

    gtk_widget_show_all (self->root);
}

static void
window_simple_changed (WnckWindow *window,
                       WorkspaceTaskbar *self)
{
    workspace_taskbar_request_rebuild (self);
}

static void
window_state_changed (WnckWindow *window,
                      WnckWindowState changed_mask,
                      WnckWindowState new_state,
                      WorkspaceTaskbar *self)
{
    workspace_taskbar_request_rebuild (self);
}

static void
watch_window (WorkspaceTaskbar *self,
              WnckWindow *window)
{
    if (g_hash_table_contains (self->watched_windows, window))
        return;

    g_hash_table_add (self->watched_windows, window);

    g_signal_connect (window,
                      "name-changed",
                      G_CALLBACK (window_simple_changed),
                      self);
    g_signal_connect (window,
                      "icon-changed",
                      G_CALLBACK (window_simple_changed),
                      self);
    g_signal_connect (window,
                      "workspace-changed",
                      G_CALLBACK (window_simple_changed),
                      self);
    g_signal_connect (window,
                      "state-changed",
                      G_CALLBACK (window_state_changed),
                      self);
}

static void
active_window_changed (WnckScreen *screen,
                       WnckWindow *previous_window,
                       WorkspaceTaskbar *self)
{
    workspace_taskbar_request_rebuild (self);
}

static void
active_workspace_changed (WnckScreen *screen,
                          WnckWorkspace *previous_workspace,
                          WorkspaceTaskbar *self)
{
    workspace_taskbar_request_rebuild (self);
}

static void
window_opened (WnckScreen *screen,
               WnckWindow *window,
               WorkspaceTaskbar *self)
{
    watch_window (self, window);
    workspace_taskbar_request_rebuild (self);
}

static void
window_closed (WnckScreen *screen,
               WnckWindow *window,
               WorkspaceTaskbar *self)
{
    g_signal_handlers_disconnect_by_data (window, self);
    g_hash_table_remove (self->watched_windows, window);
    workspace_taskbar_request_rebuild (self);
}

static void
workspace_created_or_destroyed (WnckScreen *screen,
                                WnckWorkspace *workspace,
                                WorkspaceTaskbar *self)
{
    workspace_taskbar_request_rebuild (self);
}

static void
connect_screen_signals (WorkspaceTaskbar *self)
{
    GList *windows;

    g_signal_connect (self->screen,
                      "active-window-changed",
                      G_CALLBACK (active_window_changed),
                      self);
    g_signal_connect (self->screen,
                      "active-workspace-changed",
                      G_CALLBACK (active_workspace_changed),
                      self);
    g_signal_connect (self->screen,
                      "window-opened",
                      G_CALLBACK (window_opened),
                      self);
    g_signal_connect (self->screen,
                      "window-closed",
                      G_CALLBACK (window_closed),
                      self);
    g_signal_connect (self->screen,
                      "workspace-created",
                      G_CALLBACK (workspace_created_or_destroyed),
                      self);
    g_signal_connect (self->screen,
                      "workspace-destroyed",
                      G_CALLBACK (workspace_created_or_destroyed),
                      self);

    windows = wnck_screen_get_windows (self->screen);
    for (GList *item = windows; item != NULL; item = item->next)
        watch_window (self, WNCK_WINDOW (item->data));
}

static void
workspace_taskbar_read_settings (WorkspaceTaskbar *self)
{
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *file;
    gint value;

    self->label_font_size = DEFAULT_LABEL_FONT_SIZE;
    self->icon_spacing = DEFAULT_ICON_SPACING;

    file = xfce_panel_plugin_save_location (self->plugin, TRUE);
    if (file == NULL)
        return;

    key_file = g_key_file_new ();
    if (!g_key_file_load_from_file (key_file,
                                    file,
                                    G_KEY_FILE_NONE,
                                    NULL))
    {
        g_key_file_unref (key_file);
        g_free (file);
        return;
    }

    value = g_key_file_get_integer (key_file,
                                    "WorkspaceTaskbar",
                                    "label-font-size",
                                    &error);
    if (error == NULL)
        self->label_font_size = CLAMP (value, 6, 24);
    g_clear_error (&error);

    value = g_key_file_get_integer (key_file,
                                    "WorkspaceTaskbar",
                                    "icon-spacing",
                                    &error);
    if (error == NULL)
        self->icon_spacing = CLAMP (value, 0, 16);
    g_clear_error (&error);

    g_key_file_unref (key_file);
    g_free (file);
}

static void
workspace_taskbar_save (XfcePanelPlugin *plugin,
                        WorkspaceTaskbar *self)
{
    GKeyFile *key_file;
    GError *error = NULL;
    gchar *file;

    file = xfce_panel_plugin_save_location (plugin, TRUE);
    if (file == NULL)
        return;

    key_file = g_key_file_new ();
    g_key_file_set_integer (key_file,
                            "WorkspaceTaskbar",
                            "label-font-size",
                            self->label_font_size);
    g_key_file_set_integer (key_file,
                            "WorkspaceTaskbar",
                            "icon-spacing",
                            self->icon_spacing);

    if (!g_key_file_save_to_file (key_file, file, &error))
    {
        g_warning ("Could not save Workspace Taskbar settings to %s: %s",
                   file,
                   error->message);
        g_clear_error (&error);
    }

    g_key_file_unref (key_file);
    g_free (file);
}

static void
workspace_taskbar_update_css (WorkspaceTaskbar *self)
{
    GdkScreen *gdk_screen;
    GError *error = NULL;
    gchar *css_data;

    css_data = g_strdup_printf (
        ".wt-workspace {"
        "  background-color: rgba(48,48,48,0.72);"
        "  border: 1px solid rgba(105,105,105,0.78);"
        "  border-radius: 2px;"
        "  padding: 1px 3px;"
        "}"
        ".wt-workspace.active-workspace {"
        "  background-color: rgba(76,76,76,0.92);"
        "  border-color: rgba(165,165,165,0.95);"
        "}"
        ".wt-workspace-number {"
        "  color: rgba(220,220,220,0.78);"
        "  font-size: %dpx;"
        "  font-weight: bold;"
        "  padding-right: 1px;"
        "}"
        ".wt-window-button {"
        "  min-width: 22px;"
        "  min-height: 22px;"
        "  padding: 1px 2px;"
        "  margin: 0;"
        "  border: 0;"
        "  border-radius: 2px;"
        "  background: transparent;"
        "  box-shadow: none;"
        "}"
        ".wt-window-button:hover {"
        "  background-color: rgba(255,255,255,0.12);"
        "}"
        ".wt-window-button.active-window {"
        "  background-color: rgba(255,255,255,0.20);"
        "}"
        ".wt-window-button.urgent-window {"
        "  background-color: rgba(194,90,70,0.65);"
        "}",
        self->label_font_size);

    if (self->css == NULL)
    {
        gdk_screen = gtk_widget_get_screen (GTK_WIDGET (self->plugin));
        self->css = gtk_css_provider_new ();
        gtk_style_context_add_provider_for_screen (
            gdk_screen,
            GTK_STYLE_PROVIDER (self->css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    if (!gtk_css_provider_load_from_data (self->css,
                                          css_data,
                                          -1,
                                          &error))
    {
        g_warning ("Could not load Workspace Taskbar CSS: %s",
                   error->message);
        g_clear_error (&error);
    }

    g_free (css_data);
}

static void
label_font_size_changed (GtkSpinButton *spin,
                         WorkspaceTaskbar *self)
{
    gint value = gtk_spin_button_get_value_as_int (spin);

    if (value == self->label_font_size)
        return;

    self->label_font_size = value;
    workspace_taskbar_update_css (self);
    workspace_taskbar_save (self->plugin, self);
}

static void
icon_spacing_changed (GtkSpinButton *spin,
                      WorkspaceTaskbar *self)
{
    gint value = gtk_spin_button_get_value_as_int (spin);

    if (value == self->icon_spacing)
        return;

    self->icon_spacing = value;
    workspace_taskbar_rebuild (self);
    workspace_taskbar_save (self->plugin, self);
}

static void
settings_dialog_destroyed (GtkWidget *dialog,
                           WorkspaceTaskbar *self)
{
    xfce_panel_plugin_block_autohide (self->plugin, FALSE);
    g_object_set_data (G_OBJECT (self->plugin), "settings-dialog", NULL);
}

static void
settings_dialog_response (GtkDialog *dialog,
                          gint response_id,
                          WorkspaceTaskbar *self)
{
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
workspace_taskbar_configure (XfcePanelPlugin *plugin,
                             WorkspaceTaskbar *self)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *font_spin;
    GtkWidget *spacing_spin;

    dialog = g_object_get_data (G_OBJECT (plugin), "settings-dialog");
    if (dialog != NULL)
    {
        gtk_window_present (GTK_WINDOW (dialog));
        return;
    }

    dialog = gtk_dialog_new_with_buttons (
        "Workspace Taskbar Properties",
        NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close",
        GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_icon_name (GTK_WINDOW (dialog), "preferences-system-windows");
    gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

    xfce_panel_plugin_take_window (plugin, GTK_WINDOW (dialog));
    xfce_panel_plugin_block_autohide (plugin, TRUE);
    g_object_set_data (G_OBJECT (plugin), "settings-dialog", dialog);

    content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 10);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 12);
    gtk_container_add (GTK_CONTAINER (content), grid);

    label = gtk_label_new ("Workspace label font size:");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);

    font_spin = gtk_spin_button_new_with_range (6.0, 24.0, 1.0);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (font_spin),
                               self->label_font_size);
    gtk_grid_attach (GTK_GRID (grid), font_spin, 1, 0, 1, 1);

    label = gtk_label_new ("Spacing between window icons:");
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);

    spacing_spin = gtk_spin_button_new_with_range (0.0, 16.0, 1.0);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (spacing_spin),
                               self->icon_spacing);
    gtk_grid_attach (GTK_GRID (grid), spacing_spin, 1, 1, 1, 1);

    g_signal_connect (font_spin,
                      "value-changed",
                      G_CALLBACK (label_font_size_changed),
                      self);
    g_signal_connect (spacing_spin,
                      "value-changed",
                      G_CALLBACK (icon_spacing_changed),
                      self);
    g_signal_connect (dialog,
                      "response",
                      G_CALLBACK (settings_dialog_response),
                      self);
    g_signal_connect (dialog,
                      "destroy",
                      G_CALLBACK (settings_dialog_destroyed),
                      self);

    gtk_widget_show_all (dialog);
}

static gboolean
plugin_size_changed (XfcePanelPlugin *plugin,
                     gint size,
                     WorkspaceTaskbar *self)
{
    self->panel_size = size;
    workspace_taskbar_rebuild (self);
    return TRUE;
}

static void
workspace_taskbar_free (XfcePanelPlugin *plugin,
                        WorkspaceTaskbar *self)
{
    GHashTableIter iter;
    gpointer key;

    if (self->screen != NULL)
        g_signal_handlers_disconnect_by_data (self->screen, self);

    g_hash_table_iter_init (&iter, self->watched_windows);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_signal_handlers_disconnect_by_data (G_OBJECT (key), self);

    g_clear_pointer (&self->watched_windows, g_hash_table_unref);

    if (self->css != NULL)
    {
        GdkScreen *gdk_screen = gtk_widget_get_screen (GTK_WIDGET (plugin));
        gtk_style_context_remove_provider_for_screen (
            gdk_screen,
            GTK_STYLE_PROVIDER (self->css));
    }
    g_clear_object (&self->css);
    g_clear_object (&self->wnck_handle);
    g_free (self);
}

static WorkspaceTaskbar *
workspace_taskbar_new (XfcePanelPlugin *plugin)
{
    WorkspaceTaskbar *self;

    self = g_new0 (WorkspaceTaskbar, 1);
    self->plugin = plugin;
    self->panel_size = xfce_panel_plugin_get_size (plugin);
    self->context_workspace_number = -1;
    self->watched_windows = g_hash_table_new (g_direct_hash, g_direct_equal);
    workspace_taskbar_read_settings (self);

    self->root = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROOT_SPACING);
    gtk_box_set_homogeneous (GTK_BOX (self->root), TRUE);

    self->wnck_handle = wnck_handle_new (WNCK_CLIENT_TYPE_PAGER);
    self->screen = wnck_handle_get_default_screen (self->wnck_handle);
    wnck_screen_force_update (self->screen);

    workspace_taskbar_update_css (self);
    connect_screen_signals (self);
    workspace_taskbar_rebuild (self);

    return self;
}

static void
workspace_taskbar_construct (XfcePanelPlugin *plugin)
{
    WorkspaceTaskbar *self;

    self = workspace_taskbar_new (plugin);

    gtk_container_add (GTK_CONTAINER (plugin), self->root);
    xfce_panel_plugin_set_expand (plugin, TRUE);

    self->swap_menu_item = gtk_menu_item_new_with_label ("Swap with");
    self->swap_submenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (self->swap_menu_item),
                               self->swap_submenu);
    gtk_widget_set_sensitive (self->swap_menu_item, FALSE);
    xfce_panel_plugin_menu_insert_item (
        plugin,
        GTK_MENU_ITEM (self->swap_menu_item));
    gtk_widget_show_all (self->swap_menu_item);

    xfce_panel_plugin_menu_show_configure (plugin);
    g_signal_connect (plugin,
                      "configure-plugin",
                      G_CALLBACK (workspace_taskbar_configure),
                      self);
    g_signal_connect (plugin,
                      "save",
                      G_CALLBACK (workspace_taskbar_save),
                      self);

    g_signal_connect (plugin,
                      "size-changed",
                      G_CALLBACK (plugin_size_changed),
                      self);
    g_signal_connect (plugin,
                      "free-data",
                      G_CALLBACK (workspace_taskbar_free),
                      self);

    gtk_widget_show_all (GTK_WIDGET (plugin));
}

XFCE_PANEL_PLUGIN_REGISTER (workspace_taskbar_construct)
