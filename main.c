#include <gio/gio.h>
#include <libnotify/notification.h>
#include <signal.h>
#ifdef USE_LIBNOTIFY
#include <gio/gdesktopappinfo.h>
#include <libnotify/notify.h>
#endif

#define FACCESS_ATTR "standard::*,ownser::user"

typedef struct {
	GFile *file;
	GFileType type;
	GIcon *icon;
} FileInstance;

static GList *tracked_files;

static gchar **target_files = {NULL};
static gboolean check_writes = FALSE;
static gboolean check_moves = FALSE;
static gboolean check_create = FALSE;
static gboolean check_delete = FALSE;
#ifdef USE_LIBNOTIFY
static gboolean use_notify = TRUE;

void view_file_location(NotifyNotification *notification, char *action, gpointer user_data) {
	GFile *file = G_FILE(user_data);
	GFile *parent_file = g_file_get_parent(file);
	const gchar *file_uri = g_file_get_uri(parent_file);
	g_app_info_launch_default_for_uri(file_uri, 0, 0);
	g_print("what");
	g_object_unref(file);
	g_object_unref(parent_file);
	notify_notification_close(notification, NULL);
	g_object_unref(G_OBJECT(notification));
}

void view_file(NotifyNotification *notification, char *action, gpointer user_data) {
	GFile *file = G_FILE(user_data);
	const gchar *file_uri = g_file_get_uri(file);
	g_app_info_launch_default_for_uri(file_uri, 0, 0);
	g_object_unref(file);
	notify_notification_close(notification, NULL);
	g_object_unref(G_OBJECT(notification));
}

static void display_notification(const gchar *msg, const gchar *desc,
								 const gchar *icon, NotifyActionCallback callback,
								 const gchar *action_label, GFile *target_file) {
	g_object_ref(target_file);
	NotifyNotification *notification = notify_notification_new(msg, desc, icon);
	notify_notification_add_action(notification, "view-file", action_label, callback, target_file, NULL);
	notify_notification_add_action(notification, "default", action_label, callback, target_file, NULL);
	notify_notification_show(notification, NULL);
}
#endif

static gchar *file_get_path(GFile *file) {
	gchar *filepath = g_file_get_parse_name(file);
	const gchar *home = g_get_home_dir();
	if(g_str_has_prefix(filepath, home)) {
		return g_strconcat("~", filepath + strlen(home), NULL);
	}
	return filepath;
}

static void update_tracked_file(FileInstance *instance) {
#ifdef USE_LIBNOTIFY
	if(use_notify) {
		GFileInfo *file_info;
		GAppInfo *app;
		GIcon *icon = NULL;

		if(instance->icon) {
			g_object_unref(instance->icon);
		}

		GKeyFile *keyfile = g_key_file_new();
		file_info = g_file_query_info(instance->file, FACCESS_ATTR, 0, NULL, NULL);
		if(g_key_file_load_from_file(keyfile, g_file_get_path(instance->file), G_KEY_FILE_NONE, NULL)) {
			app = G_APP_INFO(g_desktop_app_info_new_from_keyfile(keyfile));
			if(app) {
				icon = g_app_info_get_icon(app);
			}
		}
		g_key_file_unref(keyfile);

		if(!icon && file_info)
			icon = g_file_info_get_icon(file_info);
		instance->icon = icon;
	}
#endif
	instance->type =
		g_file_query_file_type(instance->file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
}

static FileInstance *add_tracked_file(GFile *file) {
	FileInstance *instance = g_malloc(sizeof(FileInstance));
	instance->file = g_file_dup(file);
	instance->icon = NULL;
	update_tracked_file(instance);
	tracked_files = g_list_prepend(tracked_files, instance);
	return instance;
}

static FileInstance *get_tracked_file(GFile *file) {
	GList *list = tracked_files;
	for(; list != NULL; list = g_list_next(list)) {
		FileInstance *data = list->data;
		if(data == NULL) {
			continue;
		}
		if(g_file_equal(file, G_FILE(data->file))) {
			return data;
		}
	}
	return NULL;
}

static void remove_tracked_file(FileInstance *instance) {
	GList *list = tracked_files;
	for(; list != NULL; list = g_list_next(list)) {
		FileInstance *data = list->data;
		if(data == NULL) {
			continue;
		}
		if(data == instance) {
			tracked_files = g_list_remove_link(tracked_files, list);
			g_object_unref(data->file);
			g_object_unref(data->icon);

			g_free(data);
			g_free(list);
		}
	}
}

static void file_changed_cb(GFileMonitor *monitor, GFile *file,
							GFile *other_file, GFileMonitorEvent evtype,
							gpointer user_data) {
	const gchar *str_type;
	const gchar *str_op;
	const gchar *str_action;
	NotifyActionCallback notify_action;
	gchar *str_desc;

	switch(evtype) {
		case G_FILE_MONITOR_EVENT_MOVED_IN:
		case G_FILE_MONITOR_EVENT_MOVED_OUT:
			if(!check_moves) {
				return;
			}
			str_op = "moved";
			str_action = "Show in Files";
			notify_action = view_file_location;
			break;
		case G_FILE_MONITOR_EVENT_DELETED:
			if(!check_delete) {
				return;
			}
			str_op = "deleted";
			str_action = "Show in Files";
			notify_action = view_file_location;
			break;
		case G_FILE_MONITOR_EVENT_CREATED:
			if(!check_create) {
				return;
			}
			str_op = "created";
			str_action = "Show in Files";
			notify_action = view_file_location;
			break;
		case G_FILE_MONITOR_EVENT_RENAMED:
			if(!check_moves) {
				return;
			}
			str_op = "renamed";
			str_action = "Show in Files";
			notify_action = view_file_location;
			break;
		case G_FILE_MONITOR_EVENT_CHANGED:
			if(!check_writes) {
				return;
			}
			// fixme: multiple changed events
			str_op = "changed";
			str_action = "View file contents";
			notify_action = view_file;
			break;
		default:
			return; // Return early if there are events we don't care about
	}

	GFile *checked_file = file;
	if(evtype == G_FILE_MONITOR_EVENT_MOVED_IN ||
	   evtype == G_FILE_MONITOR_EVENT_MOVED_OUT ||
	   evtype == G_FILE_MONITOR_EVENT_RENAMED)
		checked_file = other_file;

	FileInstance *tracked_file = get_tracked_file(checked_file);

	if(tracked_file == NULL) {
		if(g_file_query_exists(checked_file, NULL)) {
			tracked_file = add_tracked_file(checked_file);
		}
	}
	g_assert(tracked_file != NULL);

	if(evtype == G_FILE_MONITOR_EVENT_CHANGED) {
		update_tracked_file(tracked_file);
	}

	switch(tracked_file->type) {
		case G_FILE_TYPE_SYMBOLIC_LINK:
			str_type = "Symlink";
			break;
		case G_FILE_TYPE_DIRECTORY:
			str_type = "Directory";
			break;
		case G_FILE_TYPE_REGULAR:
		default:
			str_type = "File";
			break;
	}

	if(evtype == G_FILE_MONITOR_EVENT_MOVED_OUT ||
	   evtype == G_FILE_MONITOR_EVENT_MOVED_IN) {
		str_desc = g_strdup_printf("%s -> %s", file_get_path(file), file_get_path(other_file));
	} else if(evtype == G_FILE_MONITOR_EVENT_RENAMED) {
		str_desc = g_strdup_printf("%s -> %s", file_get_path(file), g_file_get_basename(other_file));
	} else {
		str_desc = g_strdup_printf("%s", file_get_path(file));
	}
#ifdef USE_LIBNOTIFY
	if(use_notify) {
		GIcon *icon = tracked_file->icon;
		gchar *iconname = NULL;

		if(icon) {
			if(G_IS_THEMED_ICON(icon)) {
				// libnotify does not provide a function which accepts GIcon
				// Also, there's no way of setting fallback icons, so we take
				// first one
				iconname =
					g_strdup(g_themed_icon_get_names(G_THEMED_ICON(icon))[0]);
			} else {
				iconname = g_icon_to_string(icon);
			}
		}

		char *msgstr = g_strdup_printf("%s %s", str_type, str_op);
		// Another event is expected to properly update file icon, or not?
		// if(!tracked_file->dirty) {
		display_notification(msgstr, str_desc, iconname, notify_action, str_action, tracked_file->file);
		// }
		g_free(iconname);
		g_free(msgstr);
		g_free(str_desc);
		if(evtype == G_FILE_MONITOR_EVENT_MOVED_OUT ||
		   evtype == G_FILE_MONITOR_EVENT_DELETED ||
		   evtype == G_FILE_MONITOR_EVENT_RENAMED) {
			// We no longer care about old file
			remove_tracked_file(tracked_file);
		}
		return;
	}
#endif
	g_print("%s %s: %s\n", str_type, str_op, str_desc);
	g_free(str_desc);
}

static GMainLoop *loop;

static GOptionEntry params[] = {
	{"file", 'f', 0, G_OPTION_ARG_FILENAME_ARRAY, &target_files, "File path(s) to monitor", NULL},
	{"writes", 'w', 0, G_OPTION_ARG_NONE, &check_writes, "Monitor file writes", NULL},
	{"movement", 'm', 0, G_OPTION_ARG_NONE, &check_moves, "Monitor file moves and renames", NULL},
	{"creation", 'c', 0, G_OPTION_ARG_NONE, &check_create, "Monitor file creation", NULL},
	{"deletion", 'd', 0, G_OPTION_ARG_NONE, &check_delete, "Monitor file deletion", NULL},
#ifdef USE_LIBNOTIFY
	{"print", 'p', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &use_notify, "Print to stdout instead of using desktop notifications", NULL},
#endif
	{NULL}
};

static void interrupt() {
	g_main_loop_quit(loop);
}

int main(int argc, char **argv) {
	GError *error = NULL;
	GFile *target_file;
	GFileType filetype;
	GFileMonitor **monitors;

	GOptionContext *context;

	GFileEnumerator *dir_enum;
	GFileInfo *file_entry;

	context = g_option_context_new("");
	g_option_context_add_main_entries(context, params, NULL);
	if(!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("Option parsing failed: %s\n", error->message);
		return 1;
	}

#ifdef USE_LIBNOTIFY
	if(use_notify)
		if(!notify_init("gio-filemonitor")) {
			g_printerr("Error initializing notification daemon\n");
			return 1;
		}
#endif
	tracked_files = g_list_alloc();
	gint i = 0;
	gint n = 0;
	const gchar *filepath = target_files[0];
	while(filepath != NULL) {
		n++;
		filepath = target_files[n];
	}
	if(n == 0) {
		g_printerr("No files to monitor!\n");
		return 1;
	}
	monitors = g_malloc(sizeof(GFileMonitor) * (n + 1));
	memset(monitors, 0, sizeof(GFileMonitor));

	filepath = target_files[0];
	while(filepath != NULL) {
		target_file = g_file_new_for_path(filepath);

		if(g_file_query_exists(target_file, NULL)) {
			filetype = g_file_query_file_type(target_file, G_FILE_QUERY_INFO_NONE, NULL);

			if(filetype == G_FILE_TYPE_DIRECTORY) {
				dir_enum = g_file_enumerate_children(target_file, FACCESS_ATTR, 0, NULL, &error);
				while((file_entry = g_file_enumerator_next_file(dir_enum, NULL, &error)) != NULL) {
					add_tracked_file(g_file_get_child(target_file, g_file_info_get_name(file_entry)));
				}
				monitors[i] = g_file_monitor_directory(target_file, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
				g_signal_connect(monitors[i], "changed", G_CALLBACK(file_changed_cb), NULL);
			} else if(filetype == G_FILE_TYPE_REGULAR) {
				add_tracked_file(target_file);
				monitors[i] = g_file_monitor_file(target_file, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
				g_signal_connect(monitors[i], "changed", G_CALLBACK(file_changed_cb), NULL);
			}
		} else {
			monitors[i] = g_file_monitor_file(target_file, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
			g_signal_connect(monitors[i], "changed", G_CALLBACK(file_changed_cb), NULL);
		}
		i++;
		filepath = target_files[i];
	}

	g_strfreev(target_files);

	loop = g_main_loop_new(NULL, TRUE);
	signal(SIGINT, interrupt);
	g_main_loop_run(loop);

#ifdef USE_LIBNOTIFY
	if(use_notify)
		notify_uninit();
#endif

	g_main_loop_unref(loop);

	g_list_free(tracked_files);

	for(i = 0; i < n; i++) {
		g_object_unref(monitors[i]);
	}
	return 0;
}
