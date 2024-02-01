# gio-filemonitor

A simple file monitoring utility using `GLib` and `libnotify`

Thanks to [hoff._world](https://www.youtube.com/@hoff._world) for the [idea](https://youtu.be/9nDYYc_7sKs)

## Building and running
```bash
USE_LIBNOTIFY=1 make release
./gio-filemonitor -h # Usage
```

## Usage
```
  -f, --file         File path(s) to monitor
  -w, --writes       Monitor file writes
  -m, --movement     Monitor file moves and renames
  -c, --creation     Monitor file creation
  -d, --deletion     Monitor file deletion
  -p, --print        Print to stdout instead of using desktop notifications
```
For example:
```bash
# Monitor creation and deletion of user specific app launchers and global app launchers 
./gio-filemonitor -f /usr/share/applications -f $XDG_DATA_HOME/applications --creation --deletion
```