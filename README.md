# honeycomb-bravo-linux

Linux support for the Honeycomb Aeronautical Bravo Throttle Quadrant
(USB `294b:1901`).

* **Kernel driver** (`driver/`): an out-of-tree HID driver. The 6 axes and
  48 buttons stay on the normal input layer (`/dev/input/js*` and
  `/dev/input/event*`, the same as `hid-generic`). The 28 panel lights are
  exposed as standard Linux LEDs under `/sys/class/leds/bravo:*`.
* **X-Plane 12 plugin** (`xplane-plugin/`): a native `lin_x64` plugin that
  drives those LEDs from simulator datarefs. It does not talk USB itself; it
  writes the sysfs brightness files the driver creates.

## Status

* **Driver:** tested on real hardware. Installed with DKMS on Debian 13
  (kernels 7.1.8 and 7.1.13) with Secure Boot, the module signed by an
  enrolled Machine Owner Key. All 28 lights verified.
* **Plugin:** builds and installs. It has not yet been flown in the simulator.

## Quick start

### Driver (DKMS)

From the repository root, with `build-essential`, `dkms`, and the headers
for your kernel installed:

```sh
sudo apt install build-essential dkms linux-headers-$(uname -r)
sudo cp -r driver /usr/src/hid-honeycomb-bravo-0.1.0
sudo dkms add     hid-honeycomb-bravo/0.1.0
sudo dkms build   hid-honeycomb-bravo/0.1.0
sudo dkms install hid-honeycomb-bravo/0.1.0
```

DKMS rebuilds the module for later kernels (`AUTOINSTALL=yes`). A plain
`make` in `driver/`, loading, and uninstall are described in
[driver/README.md](driver/README.md).

#### Secure Boot

With Secure Boot on, the kernel loads only modules signed by a key it
trusts. Debian's DKMS creates a Machine Owner Key at `/var/lib/dkms/mok.key`
and `/var/lib/dkms/mok.pub` on its first build and signs every module with
it. Enroll that key once:

```sh
sudo mokutil --import /var/lib/dkms/mok.pub   # choose a one-time password
sudo reboot
```

During that reboot the firmware shows the blue **MOK Manager** screen.
That screen is drawn only on the built-in / firmware display — an external
monitor often stays dark — and it times out quickly, continuing the boot if
you do not answer. Select *Enroll MOK* → *Continue* → *Yes*, enter the
password, then *Reboot*.

Afterwards `mokutil --test-key /var/lib/dkms/mok.pub` should report that the
key is already enrolled.

#### Lights without root

```sh
sudo install -m 644 driver/99-honeycomb-bravo.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
sudo modprobe hid-honeycomb-bravo
```

Members of `plugdev` can then write each LED's `brightness` file.
`ls /sys/class/leds | grep bravo` lists the 28 lights. `driver/bravo-leds`
is a small helper (`list`, `on`, `off`, `test`, `all-off`).

### X-Plane 12 plugin

The build needs the X-Plane SDK headers in `xplane-plugin/SDK`. The SDK is
not included here. Download the official zip from
[developer.x-plane.com](https://developer.x-plane.com/sdk/plugin-sdk-downloads/)
(this plugin was written against SDK 4.1.1; a newer zip from that page works
the same way as long as it contains `CHeaders/XPLM`):

```sh
cd xplane-plugin
./fetch-sdk.sh          # SDK 4.1.1 into ./SDK; pass 430 for SDK 4.3.0
./build.sh              # or: make
```

By hand, without the script:

```sh
cd xplane-plugin
curl -fL -o /tmp/XPSDK411.zip \
  https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK411.zip
unzip /tmp/XPSDK411.zip    # creates SDK/
./build.sh
```

Install the plugin (or `make install`, which uses the same directory):

```sh
install -m 755 build/HoneycombBravoLights/lin_x64/HoneycombBravoLights.xpl \
  ~/"X-Plane 12/Resources/plugins/HoneycombBravoLights/lin_x64/"
```

The user running X-Plane must be able to write
`/sys/class/leds/bravo:*/brightness` (the udev rule above). Dataref mapping
and tunables are in [xplane-plugin/README.md](xplane-plugin/README.md).

## LEDs

Each light is `/sys/class/leds/<name>/brightness` (`0` or `1`). The names
are one 4-byte vendor feature report (usage page `0xFF00`):

| LED name | Panel | Byte | Bit |
|---|---|---|---|
| `bravo::ap_hdg` | Autopilot HDG | 0 | 0 |
| `bravo::ap_nav` | Autopilot NAV | 0 | 1 |
| `bravo::ap_apr` | Autopilot APR | 0 | 2 |
| `bravo::ap_rev` | Autopilot REV | 0 | 3 |
| `bravo::ap_alt` | Autopilot ALT | 0 | 4 |
| `bravo::ap_vs` | Autopilot VS | 0 | 5 |
| `bravo::ap_ias` | Autopilot IAS | 0 | 6 |
| `bravo::ap_autopilot` | Autopilot | 0 | 7 |
| `bravo:green:gear_left` | Gear left, green | 1 | 0 |
| `bravo:red:gear_left` | Gear left, red | 1 | 1 |
| `bravo:green:gear_nose` | Gear nose, green | 1 | 2 |
| `bravo:red:gear_nose` | Gear nose, red | 1 | 3 |
| `bravo:green:gear_right` | Gear right, green | 1 | 4 |
| `bravo:red:gear_right` | Gear right, red | 1 | 5 |
| `bravo:red:master_warning` | Master warning | 1 | 6 |
| `bravo:red:engine_fire` | Engine fire | 1 | 7 |
| `bravo::low_oil_pressure` | Low oil pressure | 2 | 0 |
| `bravo::low_fuel_pressure` | Low fuel pressure | 2 | 1 |
| `bravo::anti_ice` | Anti ice | 2 | 2 |
| `bravo::starter_engaged` | Starter engaged | 2 | 3 |
| `bravo::apu` | APU | 2 | 4 |
| `bravo:amber:master_caution` | Master caution | 2 | 5 |
| `bravo::vacuum` | Vacuum | 2 | 6 |
| `bravo::low_hyd_pressure` | Low hydraulic pressure | 2 | 7 |
| `bravo::aux_fuel_pump` | Aux fuel pump | 3 | 0 |
| `bravo::parking_brake` | Parking brake | 3 | 1 |
| `bravo::low_volts` | Low volts | 3 | 2 |
| `bravo::door` | Door | 3 | 3 |

When both the green and red LED of one gear housing are on, green wins.
The plugin maps these to X-Plane 12 default datarefs (autopilot modes, gear
position, and the annunciator panel). Add-on aircraft that do not publish
those datarefs will not move the matching lights.

## Credits

The LED report layout and the dataref logic follow:

* Daniel Peukert, [honeycomb-xplane-linux](https://gitlab.com/dpeukert/honeycomb-xplane-linux)
* jorgeuvo, [Honeycomb-Bravo-Plugin](https://github.com/jorgeuvo/Honeycomb-Bravo-Plugin)
* Jeremie Corbier, [XHoneycombBravo](https://github.com/jcorbier/XHoneycombBravo)

## License

[GPL-2.0-only](LICENSE). The kernel module declares `MODULE_LICENSE("GPL")`.

## Author

The author writes these tools with AI for his own use. Others are welcome to use them.
