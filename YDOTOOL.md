# ydotool Setup for GNOME on Wayland

This document explains how to set up `ydotool` to work automatically on Ubuntu with GNOME/Wayland, without requiring manual permission adjustments after each boot.

**Tested on:** Ubuntu 25.04 (Plucky) with GNOME on Wayland

## The Problem

On GNOME with Wayland:
- `wtype` doesn't work because GNOME's Mutter compositor doesn't support the virtual-keyboard protocol
- `xdotool` only works with XWayland applications, not native Wayland apps
- Ubuntu's `ydotool` package (version 0.1.8-3build1) lacks proper udev rules and systemd service

## The Solution

Use **Debian's ydotool package version 1.0.4-2**, which includes:
- Proper udev rules for `/dev/uinput` access
- Systemd user service for automatic `ydotoold` daemon startup
- No manual permission granting needed after each boot

## Installation Steps

### 1. Download and Install Debian's ydotool Package

```bash
# Download the Debian package
wget http://deb.debian.org/debian/pool/main/y/ydotool/ydotool_1.0.4-2_amd64.deb

# Install it (will upgrade Ubuntu's 0.1.8 if already installed)
sudo dpkg -i ydotool_1.0.4-2_amd64.deb
```

### 2. Add Your User to the `input` Group

This allows your user to access `/dev/uinput`:

```bash
sudo usermod -aG input $USER
```

### 3. Log Out and Log Back In

The group membership change requires you to log out and log back in (or reboot) to take effect.

### 4. Verify the Setup

After logging back in, check that everything is working:

```bash
# Check group membership
groups | grep input

# Check /dev/uinput permissions
ls -l /dev/uinput
# Should show: crw-rw---- 1 root input

# Check if ydotoold service is running
systemctl --user status ydotool.service

# Test typing (will type in 3 seconds - focus a text editor!)
sleep 3 && ydotool type "Hello from ydotool!"
```

## What the Debian Package Provides

### 1. Udev Rule (`/usr/lib/udev/rules.d/80-uinput.rules`)

```
KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"
```

This rule ensures `/dev/uinput` is accessible to users in the `input` group.

### 2. Systemd User Service (`/usr/lib/systemd/user/ydotool.service`)

```ini
[Unit]
Description=Starts ydotoold service

[Service]
Type=simple
Restart=always
ExecStart=/usr/bin/ydotoold
ExecReload=/usr/bin/kill -HUP $MAINPID
KillMode=process
TimeoutSec=180

[Install]
WantedBy=default.target
```

This service:
- Starts automatically when you log in
- Restarts automatically if it crashes
- Persists across reboots

### 3. The `ydotoold` Daemon

The daemon runs in the background and handles input events from `ydotool` commands.

## How It Works

1. **Boot/Login**: Systemd automatically starts `ydotool.service` as your user
2. **Daemon**: `ydotoold` runs in the background, waiting for commands
3. **Access**: Your user (in `input` group) can write to `/dev/uinput` via the udev rule
4. **Commands**: When you run `ydotool type "text"`, it communicates with `ydotoold` to simulate typing

## Differences from Ubuntu's Package

| Feature | Ubuntu 0.1.8-3build1 | Debian 1.0.4-2 |
|---------|---------------------|----------------|
| `ydotoold` daemon | ❌ Separate package | ✅ Included |
| Udev rules | ❌ Missing | ✅ Included |
| Systemd service | ❌ Missing | ✅ Included |
| Auto-start on boot | ❌ Manual setup | ✅ Automatic |
| Requires `sudo` | ⚠️ Often needed | ✅ No |

## Usage in Scripts

In the `voice_typing_single_shot.sh` script, we:

1. **Check if ydotool is available** and prefer it over other tools
2. **Auto-start the service** if it's not running:
   ```bash
   if command -v ydotool &> /dev/null; then
       if ! systemctl --user is-active --quiet ydotool.service; then
           systemctl --user start ydotool.service
           sleep 0.5
       fi
   fi
   ```
3. **Use ydotool for typing**:
   ```bash
   ydotool type "text to type"
   ```

## Troubleshooting

### Service Not Running

```bash
# Check service status
systemctl --user status ydotool.service

# Start service manually
systemctl --user start ydotool.service

# Enable service (should already be enabled)
systemctl --user enable ydotool.service
```

### Permission Denied Errors

```bash
# Check if you're in the input group
groups | grep input

# If not, add yourself and log out/in
sudo usermod -aG input $USER
```

### /dev/uinput Not Accessible

```bash
# Reload udev rules
sudo udevadm control --reload-rules
sudo udevadm trigger

# Check permissions
ls -l /dev/uinput
# Should show: crw-rw---- 1 root input
```

## References

- [Ask Ubuntu: wtype-like tool for GNOME on Wayland](https://askubuntu.com/questions/1524311/is-there-wtype-like-tool-that-works-with-gnome-on-wayland)
- [ydotool GitHub Repository](https://github.com/ReimuNotMoe/ydotool)
- [Debian ydotool Package](https://packages.debian.org/sid/ydotool)

## Testing

After setup, you can test with:

```bash
# Focus a text editor, then run:
ydotool type "Testing ydotool on GNOME Wayland!"

# Test with special keys:
ydotool key 28:1 28:0  # Press and release Enter
```

The voice typing script will automatically use ydotool when available.

