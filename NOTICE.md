# Attribution

The Thermalright HID Type 2 packet format and SUB-dependent rotation were researched using [Lexonight1/thermalright-trcc-linux](https://github.com/Lexonight1/thermalright-trcc-linux), notably `adapters/device/hid_lcd.py` and `core/protocol.py`, licensed GPL-3.0. The protocol implementation in this project translates that documented packet structure into C++/libusb. Thanks to the original reverse-engineering contributors.

This project is distributed under GPL-3.0-or-later. It does not install, import, execute or require TRCC. The native renderer, telemetry, audio visualizations, panel and local IPC are implemented in this repository. The original TRCC themes, shaders and presets are not bundled.
