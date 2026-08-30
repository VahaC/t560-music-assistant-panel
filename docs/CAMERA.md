# Camera motion detection extension

The UI process should not perform camera analysis. On this ARMv7 tablet, a
separate low-priority daemon is safer: a camera failure cannot freeze the music
controls, and the daemon can be stopped when the screen or camera is not needed.

Recommended first implementation:

1. Read the tablet camera through V4L2 at 320x240, grayscale, 2-5 frames/s.
2. Compare blurred consecutive frames in small blocks.
3. Require motion in a configurable percentage of blocks for several frames.
4. Apply a cooldown before sending another event.
5. Publish motion to Home Assistant through MQTT or a configured webhook.

This is classic motion detection, not face/object recognition. It avoids
OpenCV, TensorFlow, and neural-network runtimes and is realistic on the legacy
3.10.17 ARMv7 kernel. Person or face recognition should only be considered
after V4L2 capture and CPU usage are measured on the physical tablet. A useful
later split is to let the tablet detect motion and send a JPEG to a stronger LAN
server for object recognition.

Before implementing the daemon, identify the working camera node and formats:

```sh
apk add v4l-utils
v4l2-ctl --list-devices
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

Do not assume `/dev/video0`: Samsung downstream camera drivers often expose
several nodes, and some are not capture devices.

