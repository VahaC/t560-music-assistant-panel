# T560 Music Panel

A native touch-only controller for the Samsung Galaxy Tab E SM-T560
(`gtelwifi`, ARMv7) running postmarketOS and Openbox. It follows the interaction
model of the
[VahaC Music Assistant ESP32 controller](https://github.com/VahaC/music-assistant-esp32s34848s040-controller),
but runs as a lightweight GTK3 application on the tablet.

The application does not use WebKit, a browser, HTML/JavaScript, text fields,
or an on-screen keyboard. All settings are edited over SSH.

## Implemented features

- Native dark UI designed for the 800x1219 usable screen area.
- Album art, track title, artist, playback progress, and volume.
- Previous, Play/Pause, Next, Volume Down, and Volume Up controls.
- Shuffle and Repeat Off/All/One controls.
- Queue browsing and playback of a selected queue item without replacing the queue.
- Playlist browsing and playback through `music_assistant.play_media`.
- Light 1, Light 2, Fan, and AC room controls.
- Direct Home Assistant REST API access without loading Lovelace.
- A separate token file with `0600` permissions.
- Watchdog, desktop entry, Openbox rule, and ARMv7 `APKBUILD`.
- A short Power press turns the display off; any key or touchscreen input wakes it.
- The existing long-press Power menu and physical Home button behavior are retained.

## Tablet configuration files

```text
~/.config/t560-music-panel/config.ini
~/.config/t560-music-panel/token
```

The first-installation procedure transfers [config.ini.example](config/config.ini.example)
into the user-owned configuration directory. Replace every placeholder with
the entity IDs created by Home Assistant and the Media Controller integration.
Put the long-lived access token in the separate `token` file. Never commit the
token.

The Media Controller integration from the ESP32 controller repository must be
installed and configured in Home Assistant first. It creates the queue and
playlist sensors and the room proxy entities consumed by this application.

## Source architecture

The application is split into focused C modules with explicit interfaces:

- `application` owns the application lifecycle and coordinates UI events with
  Home Assistant state;
- `app_config` validates and owns configuration data;
- `home_assistant_client` encapsulates authenticated asynchronous HTTP I/O;
- `panel_ui` builds and updates GTK widgets without knowing API details;
- `json_helpers` contains reusable, unit-tested JSON accessors;
- `main` is the minimal process entry point.

The Makefile tracks header dependencies automatically. Run `make test` for the
unit tests and `make` for the application.

## Build and installation

The complete guide contains two end-to-end procedures:

- first installation on an unmodified tablet user account;
- updating an existing installation without replacing configuration or tokens.

See [BUILD_AND_INSTALL.md](docs/BUILD_AND_INSTALL.md).

The repository root already contains a verified `t560-panel` binary. It is a
44 KB ELF 32-bit ARM EABI5/musl executable built in a clean Alpine 3.20 ARMv7
container. The source also compiles successfully on x86_64 Alpine as an API
compatibility check. Checksums are stored in `SHA256SUMS`.

The recommended deployment is rootless. The executable and launch scripts are
installed below `/home/vahac/.local`, and files are transferred through the
existing SSH connection without requiring SCP, SFTP, or administrative access.

## Openbox autostart

The ready-to-use [t560-openbox-autostart](openbox/t560-openbox-autostart)
starts Tint2, the cursor helper, and the included Power button handler. A short
Power press turns the display off, while any key or touchscreen input wakes it.
The handler retains the existing long-press Power menu. The autostart does not
start Badwolf, WebKit, or Matchbox Keyboard. Back up the current autostart file
before replacing it.

The first-installation procedure transfers the provided file as a candidate,
backs up the current Openbox autostart, tests the panel, and activates the new
autostart only after the test succeeds. It does not replace `rc.xml`.

## Alpine APK

The [APKBUILD](packaging/APKBUILD) limits the package to `armv7`. APK packaging
is optional and is not the recommended installation method on the current
tablet because it requires intentionally configured administrative access.

## Camera extension

The future motion-detection architecture is documented in
[CAMERA.md](docs/CAMERA.md). A realistic first stage on this CPU is V4L2 capture
at 320x240 and 2-5 FPS with a lightweight frame-difference daemon. Object
recognition should run on a more powerful LAN server after the tablet detects
motion.
