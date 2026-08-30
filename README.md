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
- No interception of the existing physical Home or Power button handlers.

## Tablet configuration files

```text
~/.config/t560-music-panel/config.ini
~/.config/t560-music-panel/token
```

Copy [config.ini.example](config/config.ini.example) to `config.ini` and replace
every placeholder with the entity IDs created by Home Assistant and the Media
Controller integration. Put the long-lived access token in the separate
`token` file. Never commit the token.

```sh
mkdir -p ~/.config/t560-music-panel
cp /etc/t560-music-panel/config.ini.example ~/.config/t560-music-panel/config.ini
chmod 700 ~/.config/t560-music-panel
chmod 600 ~/.config/t560-music-panel/config.ini ~/.config/t560-music-panel/token
```

The Media Controller integration from the ESP32 controller repository must be
installed and configured in Home Assistant first. It creates the queue and
playlist sensors and the room proxy entities consumed by this application.

## Building on the SM-T560

The repository root already contains a verified `t560-panel` binary. It is a
44 KB ELF 32-bit ARM EABI5/musl executable built in a clean Alpine 3.20 ARMv7
container. The source also compiles successfully on x86_64 Alpine as an API
compatibility check. Checksums are stored in `SHA256SUMS`.

If the postmarketOS edge libraries are ABI-incompatible with the provided
binary, rebuild the same source directly on the tablet:

```sh
apk add build-base gtk+3.0-dev libsoup3-dev json-glib-dev
make -j1
doas make install
```

Use `-j1` on this memory-constrained tablet. Runtime dependencies are
`gtk+3.0`, `libsoup3`, `json-glib`, and `xdotool`.

## Openbox autostart

The ready-to-use [t560-openbox-autostart](openbox/t560-openbox-autostart)
preserves Tint2, the cursor helper, and the existing Power button handler. It
does not start Badwolf, WebKit, or Matchbox Keyboard. Back up the current
autostart file before replacing it.

The minimal manual change to the current autostart file is:

```sh
# Remove or comment out:
# "$HOME/.local/bin/t560-badwolf-watchdog" &

# Add:
/usr/bin/t560-panel-watchdog &
```

Merge the block from [application.xml](openbox/application.xml) inside the
`<applications>` element in `~/.config/openbox/rc.xml`, then run:

```sh
openbox --reconfigure
```

## Alpine APK

The [APKBUILD](packaging/APKBUILD) limits the package to `armv7`. For a local
APK build, create a source archive containing a directory named
`t560-music-panel-0.1.0`, run `abuild checksum`, and then run `abuild -r`.

## Camera extension

The future motion-detection architecture is documented in
[CAMERA.md](docs/CAMERA.md). A realistic first stage on this CPU is V4L2 capture
at 320x240 and 2-5 FPS with a lightweight frame-difference daemon. Object
recognition should run on a more powerful LAN server after the tablet detects
motion.

