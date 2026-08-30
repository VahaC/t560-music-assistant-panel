# Build and installation guide

This project builds a native application for the existing postmarketOS
installation. It does not build or flash a postmarketOS disk image, boot image,
kernel, recovery, or Android firmware.

The supported deployment artifacts are:

1. A small dynamically linked ARMv7 executable named `t560-panel`.
2. An optional Alpine APK package named `t560-music-panel`.

The direct executable installation is the simplest method and is recommended
for initial testing. Build an APK after the application has been verified on
the physical tablet.

## Requirements

Target device:

- Samsung Galaxy Tab E SM-T560 (`gtelwifi`), not the SM-T561.
- 32-bit ARMv7 postmarketOS/Alpine.
- X11 with Openbox.
- GTK3, libsoup 3, and JSON-GLib.
- A working network connection to Home Assistant.
- The Media Controller Home Assistant integration from the related ESP32
  controller project.

Build host options:

- Windows with Docker Desktop and Linux ARM emulation; or
- the SM-T560 itself over SSH; or
- another Alpine ARMv7 machine.

## Open the source project

Open the complete folder in Visual Studio Code:

```powershell
code D:\Sources\t560
```

The application source is in `src/main.c`. The project uses a standard
`Makefile`; no IDE-specific project format is required.

## Method 1: Build an ARMv7 executable on Windows

Start Docker Desktop, open PowerShell, and run:

```powershell
cd D:\Sources\t560

docker run --rm --platform linux/arm/v7 `
  -v "D:\Sources\t560:/src" `
  -w /src alpine:3.20 `
  sh -lc "apk add --no-cache build-base gtk+3.0-dev libsoup3-dev json-glib-dev file && make clean && make && file t560-panel"
```

The output file is:

```text
D:\Sources\t560\t560-panel
```

The final `file` output must identify it as an ELF 32-bit ARM EABI5 executable
using `/lib/ld-musl-armhf.so.1`. Do not copy an x86_64 test build to the tablet.

The application cannot be run directly on Windows because it is an ARM Linux
executable.

## Method 2: Build directly on the tablet

Copy the project from Windows:

```powershell
scp -r D:\Sources\t560 vahac@192.168.1.105:/home/vahac/
```

Use the tablet's current IP address if it is no longer `192.168.1.105`.

Connect over SSH and install build dependencies:

```sh
ssh vahac@192.168.1.105
cd /home/vahac/t560
doas apk add build-base gtk+3.0-dev libsoup3-dev json-glib-dev
```

Build with a single compiler job to limit peak memory use:

```sh
make clean
make -j1
file t560-panel
```

The result must be a 32-bit ARM EABI5/musl executable.

## Install the direct executable

Install runtime dependencies on the tablet:

```sh
doas apk add gtk+3.0 libsoup3 json-glib xdotool
```

From the project directory, install the application and support files:

```sh
doas make install
```

This installs:

```text
/usr/bin/t560-panel
/usr/bin/t560-panel-watchdog
/usr/bin/t560-open-panel
/usr/share/applications/t560-music-panel.desktop
/etc/t560-music-panel/config.ini.example
```

To install only a prebuilt executable instead, copy it to the tablet and use
an explicit destination:

```powershell
scp D:\Sources\t560\t560-panel vahac@192.168.1.105:/tmp/t560-panel
```

```sh
doas install -m 0755 /tmp/t560-panel /usr/bin/t560-panel
```

The watchdog and launcher scripts are still required for the documented
Openbox integration. Install them with `doas make install` or copy them
separately.

## Configure Home Assistant access

Create the per-user configuration directory:

```sh
mkdir -p "$HOME/.config/t560-music-panel"
chmod 700 "$HOME/.config/t560-music-panel"
cp /etc/t560-music-panel/config.ini.example \
  "$HOME/.config/t560-music-panel/config.ini"
chmod 600 "$HOME/.config/t560-music-panel/config.ini"
```

Edit `config.ini` over SSH. Replace every placeholder entity ID with the actual
entity ID shown by Home Assistant:

```sh
vi "$HOME/.config/t560-music-panel/config.ini"
```

Required entities:

- Music Assistant `media_player`.
- Media Controller queue sensor.
- Media Controller playlists sensor.

Light 1, Light 2, Fan, and AC are optional. Remove an unused key instead of
leaving its placeholder value.

Create a dedicated Home Assistant long-lived access token. Store only the
token value in the separate token file:

```sh
vi "$HOME/.config/t560-music-panel/token"
chmod 600 "$HOME/.config/t560-music-panel/token"
```

Never add the token, Home Assistant credentials, Wi-Fi credentials, or SSH
secrets to this repository.

## Test before enabling autostart

Run the application from the tablet's graphical session:

```sh
DISPLAY=:0 GTK_THEME=Adwaita:dark /usr/bin/t560-panel
```

Verify all of the following:

1. The window opens maximized inside the area not occupied by Tint2.
2. The status changes from `Connecting` to `Connected`.
3. Album art, track title, artist, progress, and volume appear.
4. Previous, Play/Pause, Next, Volume Down, and Volume Up work.
5. Shuffle and Repeat reflect Home Assistant state.
6. Queue and playlist selection work with touch only.
7. Configured room controls reflect state and toggle correctly.
8. The application never opens Matchbox Keyboard.
9. The physical Home and Power button behavior remains unchanged.

If the status shows HTTP 401, replace the token. If an entity remains empty,
verify its exact entity ID in `config.ini`.

## Enable Openbox autostart

Back up the current autostart file first:

```sh
cp "$HOME/.config/openbox/autostart" \
  "$HOME/.config/openbox/autostart.before-t560-music-panel"
```

There are two installation options.

### Minimal change

Edit the existing Openbox autostart file. Remove or comment out:

```sh
"$HOME/.local/bin/t560-badwolf-watchdog" &
```

Add:

```sh
/usr/bin/t560-panel-watchdog &
```

This preserves all other existing startup helpers.

### Complete provided autostart

The project includes `openbox/t560-openbox-autostart`. It preserves Tint2, the
cursor helper, and the existing Power button handler, but does not start
Badwolf, WebKit, or Matchbox Keyboard.

Review it before installation, then run:

```sh
install -m 0755 openbox/t560-openbox-autostart \
  "$HOME/.config/openbox/autostart"
```

Merge the application block from `openbox/application.xml` inside the existing
`<applications>` element in `$HOME/.config/openbox/rc.xml`. Do not replace the
entire `rc.xml`, because it contains the existing physical button mappings.

Apply the Openbox configuration:

```sh
openbox --reconfigure
```

Log out and back in, or reboot once, to test the complete startup sequence.

## Logs and process control

The watchdog writes application output to:

```text
~/.local/state/t560-music-panel.log
```

Follow the log over SSH:

```sh
tail -f "$HOME/.local/state/t560-music-panel.log"
```

Stop the watchdog before stopping the application, otherwise it restarts the
application after two seconds:

```sh
pkill -f '/usr/bin/t560-panel-watchdog'
pkill -x t560-panel
```

Start it again with:

```sh
/usr/bin/t560-panel-watchdog &
```

## Update an existing installation

Build a new executable, stop the watchdog, replace the installed file, and
start the watchdog again:

```sh
pkill -f '/usr/bin/t560-panel-watchdog'
pkill -x t560-panel
doas install -m 0755 ./t560-panel /usr/bin/t560-panel
/usr/bin/t560-panel-watchdog &
```

Configuration and tokens in `$HOME/.config/t560-music-panel` are not replaced
by `make install`.

## Build and install an Alpine APK

Build the APK on the tablet or another Alpine ARMv7 system. Alpine does not
automatically cross-compile an ARMv7 APK from an x86_64 `abuild` environment.

Install the Alpine packaging tools and give the SSH user access to `abuild`:

```sh
doas apk add alpine-sdk
doas addgroup vahac abuild
```

Log out and reconnect so the new group is active. Create a local signing key if
one does not already exist:

```sh
abuild-keygen -a -i
```

Create the source archive expected by `packaging/APKBUILD`:

```sh
cd /home/vahac
cp -a t560 t560-music-panel-0.1.0
tar -czf t560/packaging/t560-music-panel-0.1.0.tar.gz \
  --exclude='.git' \
  --exclude='t560-panel' \
  t560-music-panel-0.1.0
cd t560/packaging
```

Generate the real checksum, build, and sign the package:

```sh
abuild checksum
abuild -r
```

The resulting package is normally placed below:

```text
~/packages/*/armv7/t560-music-panel-0.1.0-r0.apk
```

Install the exact generated file with:

```sh
doas apk add --allow-untrusted \
  "$HOME/packages"/*/armv7/t560-music-panel-0.1.0-r0.apk
```

If the local signing key was installed into Alpine's trusted keys by
`abuild-keygen -i`, `--allow-untrusted` is not required.

The APK installs the application files but intentionally does not write a Home
Assistant URL, entity ID, or token into a user's home directory.

## Roll back to Badwolf

Stop the native panel:

```sh
pkill -f '/usr/bin/t560-panel-watchdog'
pkill -x t560-panel
```

Restore the saved Openbox autostart file:

```sh
cp "$HOME/.config/openbox/autostart.before-t560-music-panel" \
  "$HOME/.config/openbox/autostart"
```

Restore the previous Openbox application configuration if it was modified,
then run:

```sh
openbox --reconfigure
```

The Home Assistant configuration and token can remain in place for a later
retry. No postmarketOS reflash is required.

