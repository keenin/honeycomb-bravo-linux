# hid-honeycomb-bravo

Linux kernel HID driver for the **Honeycomb Bravo Throttle Quadrant**
(USB `294b:1901`).

* Levers, buttons and switches keep working exactly as with `hid-generic`
  (6 axes, 48 buttons, `/dev/input/js*` and `/dev/input/event*`).
* The 28 panel lights become standard Linux LEDs under
  `/sys/class/leds/bravo:*`, switchable by writing `0`/`1` to `brightness`.

> Status: tested on hardware (Debian 13, kernels 7.1.8 and 7.1.13, Secure Boot
> with a MOK-signed module; all 28 lights verified).

## LEDs

| LED name                     | report byte | bit |
|------------------------------|-------------|-----|
| `bravo::ap_hdg`              | 0 | 0 |
| `bravo::ap_nav`              | 0 | 1 |
| `bravo::ap_apr`              | 0 | 2 |
| `bravo::ap_rev`              | 0 | 3 |
| `bravo::ap_alt`              | 0 | 4 |
| `bravo::ap_vs`               | 0 | 5 |
| `bravo::ap_ias`              | 0 | 6 |
| `bravo::ap_autopilot`        | 0 | 7 |
| `bravo:green:gear_left`      | 1 | 0 |
| `bravo:red:gear_left`        | 1 | 1 |
| `bravo:green:gear_nose`      | 1 | 2 |
| `bravo:red:gear_nose`        | 1 | 3 |
| `bravo:green:gear_right`     | 1 | 4 |
| `bravo:red:gear_right`       | 1 | 5 |
| `bravo:red:master_warning`   | 1 | 6 |
| `bravo:red:engine_fire`      | 1 | 7 |
| `bravo::low_oil_pressure`    | 2 | 0 |
| `bravo::low_fuel_pressure`   | 2 | 1 |
| `bravo::anti_ice`            | 2 | 2 |
| `bravo::starter_engaged`     | 2 | 3 |
| `bravo::apu`                 | 2 | 4 |
| `bravo:amber:master_caution` | 2 | 5 |
| `bravo::vacuum`              | 2 | 6 |
| `bravo::low_hyd_pressure`    | 2 | 7 |
| `bravo::aux_fuel_pump`       | 3 | 0 |
| `bravo::parking_brake`       | 3 | 1 |
| `bravo::low_volts`           | 3 | 2 |
| `bravo::door`                | 3 | 3 |

The lights live in one unnumbered vendor-defined (usage page 0xFF00) feature
report of 4 bytes, sent with a USB SET_REPORT request. Layout cross-checked
against:

* Daniel Peukert, honeycomb-xplane-linux (`HoneycombBravoHelper.lua`) –
  https://gitlab.com/dpeukert/honeycomb-xplane-linux
* jorgeuvo, Honeycomb-Bravo-Plugin (`Honeycomb Bravo.lua`) –
  https://github.com/jorgeuvo/Honeycomb-Bravo-Plugin
* Jeremie Corbier, XHoneycombBravo (`src/hid/mod.rs`) –
  https://github.com/jcorbier/XHoneycombBravo

The Linux helper sends 4 data bytes (matching the report descriptor); the
Windows/macOS tools pad the report to 64 bytes. If the lights ever ignore the
4-byte report, load the module with `report_len=64`.

When both the green and red LED of one gear housing are on, green wins.

## Build

```sh
make                         # against the running kernel
make KVER=7.1.13+deb13-amd64 # against another installed kernel
```

Needs `build-essential` and `linux-headers-$(uname -r)`. If `pahole`
(package `dwarves`) is missing, BTF generation is skipped automatically.

## Install (DKMS, recommended)

```sh
sudo apt install dkms
# from the repository root (this directory is driver/):
sudo cp -r driver /usr/src/hid-honeycomb-bravo-0.1.0
sudo dkms add     hid-honeycomb-bravo/0.1.0
sudo dkms build   hid-honeycomb-bravo/0.1.0
sudo dkms install hid-honeycomb-bravo/0.1.0
# also build for any other installed kernel, e.g. the one pending a reboot:
sudo dkms install hid-honeycomb-bravo/0.1.0 -k 7.1.13+deb13-amd64
```

DKMS rebuilds the module automatically for future kernels (`AUTOINSTALL=yes`).

### Secure Boot (Debian)

With Secure Boot on, the kernel only loads modules signed by a key it trusts.
Debian's DKMS creates a Machine Owner Key at `/var/lib/dkms/mok.key` /
`/var/lib/dkms/mok.pub` on its first build and signs every module it builds
with it. Enroll that key once:

```sh
sudo mokutil --import /var/lib/dkms/mok.pub   # choose a one-time password
sudo reboot
```

On the blue **MOK Manager** screen during boot: *Enroll MOK* → *Continue* →
*Yes* → enter the password → *Reboot*. That screen is drawn only on the
built-in / firmware display (an external monitor often stays dark) and it
times out quickly, continuing the boot if you do not answer. Check afterwards
with `mokutil --test-key /var/lib/dkms/mok.pub` ("is already enrolled").

### udev rule (lights without root)

From this directory (`driver/`):

```sh
sudo install -m 644 99-honeycomb-bravo.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
```

Members of `plugdev` can then write the LED `brightness`/`trigger` files and
open the Bravo's `/dev/hidraw*` node.

### Load

```sh
sudo modprobe hid-honeycomb-bravo
```

The HID core automatically moves the Bravo from `hid-generic` to this driver
when the module loads (and udev autoloads it on boot/plug via its modalias).
Check with `ls /sys/class/leds | grep bravo` and
`sudo dmesg | grep -i bravo`. If it is still on `hid-generic`, unplug and
replug the Bravo, or rebind by hand:

```sh
dev=$(basename /sys/bus/hid/drivers/hid-generic/0003:294B:1901.*)
echo "$dev" | sudo tee /sys/bus/hid/drivers/hid-generic/unbind
echo "$dev" | sudo tee /sys/bus/hid/drivers/honeycomb-bravo/bind
```

## Usage

```sh
./bravo-leds list
./bravo-leds on gear_left master_warning
./bravo-leds off bravo:red:gear_left
./bravo-leds test          # chase through all 28 lights
./bravo-leds all-off
# or directly:
echo 1 > /sys/class/leds/bravo:amber:master_caution/brightness
```

Standard LED triggers work too, e.g.
`echo timer > /sys/class/leds/bravo:red:master_warning/trigger` blinks it.

All lights are switched off when the driver binds and when it unbinds.

## Uninstall

```sh
sudo modprobe -r hid-honeycomb-bravo      # Bravo falls back to hid-generic
sudo dkms remove hid-honeycomb-bravo/0.1.0 --all
sudo rm -r /usr/src/hid-honeycomb-bravo-0.1.0
sudo rm /etc/udev/rules.d/99-honeycomb-bravo.rules
sudo udevadm control --reload
# optional, only if no other DKMS module needs it:
# sudo mokutil --delete /var/lib/dkms/mok.pub && sudo reboot
```

## License

GPL-2.0-only.
