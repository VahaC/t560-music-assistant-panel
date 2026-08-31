#!/usr/bin/python3

"""Turn the display off on a short Power press and wake it on any input."""

import ctypes
import os
import select
import subprocess
import sys
import time


KEY_PRESS = 2
KEY_RELEASE = 3
KEY_PRESS_MASK = 1 << 0
KEY_RELEASE_MASK = 1 << 1
GRAB_MODE_ASYNC = 1
ANY_MODIFIER = 1 << 15
XF86_POWER_OFF = 0x1008FF2A
DPMS_MODE_ON = 0
DPMS_MODE_OFF = 3
LONG_PRESS_SECONDS = 1.5
POLL_SECONDS = 0.5


class XKeyEvent(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("serial", ctypes.c_ulong),
        ("send_event", ctypes.c_int),
        ("display", ctypes.c_void_p),
        ("window", ctypes.c_ulong),
        ("root", ctypes.c_ulong),
        ("subwindow", ctypes.c_ulong),
        ("time", ctypes.c_ulong),
        ("x", ctypes.c_int),
        ("y", ctypes.c_int),
        ("x_root", ctypes.c_int),
        ("y_root", ctypes.c_int),
        ("state", ctypes.c_uint),
        ("keycode", ctypes.c_uint),
        ("same_screen", ctypes.c_int),
    ]


class XEvent(ctypes.Union):
    _fields_ = [
        ("type", ctypes.c_int),
        ("xkey", XKeyEvent),
        ("pad", ctypes.c_long * 24),
    ]


x11 = ctypes.CDLL("libX11.so.6")
xext = ctypes.CDLL("libXext.so.6")

x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XSelectInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_long]
x11.XGrabKey.argtypes = [
    ctypes.c_void_p,
    ctypes.c_int,
    ctypes.c_uint,
    ctypes.c_ulong,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
]
x11.XPending.argtypes = [ctypes.c_void_p]
x11.XPending.restype = ctypes.c_int
x11.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.POINTER(XEvent)]
x11.XConnectionNumber.argtypes = [ctypes.c_void_p]
x11.XConnectionNumber.restype = ctypes.c_int
x11.XFlush.argtypes = [ctypes.c_void_p]

xext.DPMSEnable.argtypes = [ctypes.c_void_p]
xext.DPMSForceLevel.argtypes = [ctypes.c_void_p, ctypes.c_ushort]
xext.DPMSInfo.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_ushort),
    ctypes.POINTER(ctypes.c_int),
]


def log(message):
    print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} {message}", flush=True)


display_name = os.environ.get("DISPLAY", ":0").encode()
display = x11.XOpenDisplay(display_name)
if not display:
    log("ERROR: could not open X display")
    sys.exit(1)

root = x11.XDefaultRootWindow(display)
keycode = int(x11.XKeysymToKeycode(display, XF86_POWER_OFF))
if not keycode:
    log("ERROR: XF86PowerOff is not mapped")
    sys.exit(1)

# Prevent synthetic X autorepeat from looking like multiple short presses.
subprocess.run(
    ["xset", "-r", str(keycode)],
    env={**os.environ, "DISPLAY": ":0"},
    check=False,
)
subprocess.run(
    ["xset", "dpms", "0", "0", "0"],
    env={**os.environ, "DISPLAY": ":0"},
    check=False,
)
xext.DPMSEnable(display)
x11.XSelectInput(display, root, KEY_PRESS_MASK | KEY_RELEASE_MASK)
x11.XGrabKey(
    display,
    keycode,
    ANY_MODIFIER,
    root,
    0,
    GRAB_MODE_ASYNC,
    GRAB_MODE_ASYNC,
)
x11.XFlush(display)


def dpms_level():
    level = ctypes.c_ushort(DPMS_MODE_ON)
    enabled = ctypes.c_int(0)
    if xext.DPMSInfo(display, ctypes.byref(level), ctypes.byref(enabled)):
        return int(level.value)
    return DPMS_MODE_ON


def force_dpms(level):
    xext.DPMSForceLevel(display, level)
    x11.XFlush(display)


monitor_on = dpms_level() == DPMS_MODE_ON
pressed = False
press_started = 0.0
woke_from_off = False
long_fired = False
next_poll = time.monotonic() + POLL_SECONDS
connection = x11.XConnectionNumber(display)
event = XEvent()

log(f"started: keycode={keycode}, monitor_on={monitor_on}")

while True:
    now = time.monotonic()
    deadlines = [next_poll]
    if pressed and not long_fired:
        deadlines.append(press_started + LONG_PRESS_SECONDS)
    timeout = max(0.0, min(deadlines) - now)
    select.select([connection], [], [], timeout)

    while x11.XPending(display):
        x11.XNextEvent(display, ctypes.byref(event))
        now = time.monotonic()

        if event.type == KEY_PRESS and int(event.xkey.keycode) == keycode:
            if not pressed:
                woke_from_off = not monitor_on
                if woke_from_off:
                    force_dpms(DPMS_MODE_ON)
                    monitor_on = True
                pressed = True
                long_fired = False
                press_started = now
                log(f"press: woke_from_off={woke_from_off}")

        elif event.type == KEY_RELEASE and int(event.xkey.keycode) == keycode:
            if pressed:
                duration = now - press_started
                if not long_fired:
                    if woke_from_off:
                        force_dpms(DPMS_MODE_ON)
                        monitor_on = True
                        log(f"short release ({duration:.2f}s): backlight on")
                    else:
                        force_dpms(DPMS_MODE_OFF)
                        monitor_on = False
                        log(f"short release ({duration:.2f}s): backlight off")
                else:
                    log(f"long release ({duration:.2f}s)")
                pressed = False
                woke_from_off = False

    now = time.monotonic()
    if pressed and not long_fired and now - press_started >= LONG_PRESS_SECONDS:
        force_dpms(DPMS_MODE_ON)
        monitor_on = True
        subprocess.Popen(
            ["/home/vahac/.local/bin/t560-power-menu"],
            env={**os.environ, "DISPLAY": ":0"},
            start_new_session=True,
        )
        long_fired = True
        log("long press: opened power menu")

    if now >= next_poll:
        if not pressed:
            actual_on = dpms_level() == DPMS_MODE_ON
            if actual_on != monitor_on:
                monitor_on = actual_on
                if actual_on:
                    log("wake: input turned backlight on")
                else:
                    log("backlight turned off externally")
        next_poll = now + POLL_SECONDS
