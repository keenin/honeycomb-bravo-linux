# HoneycombBravoLights - X-Plane 12 plugin for the Honeycomb Bravo lights (Linux)

Native X-Plane 12 plugin (lin_x64) that lights the Bravo's 28 LEDs (autopilot
mode buttons, gear lights, annunciator panel) from the sim's state.

It does not talk USB itself. It writes `0`/`1` to the LED files that the
`hid-honeycomb-bravo` kernel driver (`../driver` in this repository) creates:
`/sys/class/leds/bravo:*/brightness`. Your user must be able to write those
(the udev rule gives the `plugdev` group access).

* Runs a flight loop at ~10 Hz and only writes a light when it changes.
* All lights stay off unless some electrical bus has power
  (`sim/cockpit2/electrical/bus_volts` > 1 V), and go off when the plugin is
  disabled or X-Plane quits.
* If the Bravo is unplugged or the driver isn't loaded, it logs that once and
  quietly re-checks every ~2 s. When the Bravo comes back, it picks the lights up again.

Installed at: `~/X-Plane 12/Resources/plugins/HoneycombBravoLights/lin_x64/HoneycombBravoLights.xpl`

## Dataref -> light table (X-Plane 12 default datarefs)

| Light | Dataref(s) | Lit when |
|---|---|---|
| (all) | `sim/cockpit2/electrical/bus_volts[0..5]` | any bus > 1 V (otherwise everything off) |
| HDG | `sim/cockpit2/autopilot/heading_status` | >= 1 |
| NAV | `sim/cockpit2/autopilot/nav_status`, `gpss_status` | either >= 1 (armed or captured) |
| APR | `sim/cockpit2/autopilot/approach_status` | >= 1 |
| REV | `sim/cockpit2/autopilot/backcourse_status` | >= 1 |
| ALT | `sim/cockpit2/autopilot/altitude_hold_status` | == 2 (captured, like the reference scripts) |
| VS | `sim/cockpit2/autopilot/vvi_status` | >= 1 |
| IAS | `sim/cockpit2/autopilot/speed_status`, `autothrottle_on` | speed-by-pitch (FLC) active or autothrottle on |
| AUTOPILOT | `sim/cockpit2/autopilot/servos_on` | 1 |
| Gear N / L / R green | `sim/flightmodel2/gear/deploy_ratio[0/1/2]` | ratio = 1 (down and locked) |
| Gear N / L / R red | same | 0 < ratio < 1 (in transit), retractable gear only |
| (gear type) | `sim/aircraft/gear/acf_gear_retract` | fixed gear: green when down, never red (`FIXED_GEAR_SHOW_GREEN`) |
| MASTER WARNING | `sim/cockpit2/annunciators/master_warning` | != 0 |
| ENGINE FIRE | `sim/cockpit2/annunciators/engine_fires[0..7]` | any engine |
| LOW OIL PRESS | `sim/cockpit2/annunciators/oil_pressure_low[0..7]` | any engine |
| LOW FUEL PRESS | `sim/cockpit2/annunciators/fuel_pressure_low[0..7]` | any engine |
| ANTI ICE | `sim/cockpit2/ice/ice_pitot_heat_on_pilot` | pitot heat ON (`ANTI_ICE_LIT_WHEN_OFF` flips it) |
| STARTER ENGAGED | `sim/cockpit2/engine/actuators/starter_hit[0..7]` | any engine |
| APU | `sim/cockpit2/electrical/APU_running` | 1 |
| MASTER CAUTION | `sim/cockpit2/annunciators/master_caution` | != 0 |
| VACUUM | `sim/cockpit2/annunciators/low_vacuum` | != 0 |
| LOW HYD PRESS | `sim/cockpit2/annunciators/hydraulic_pressure` | != 0; always off for C172, SR20/22, S22T, P28A, C208, K100, KODI, DA40, RV10 (by `sim/aircraft/view/acf_ICAO`) |
| AUX FUEL PUMP | `sim/cockpit2/fuel/transfer_pump_left/right`, `sim/cockpit2/engine/actuators/fuel_pump_on[engines]` | transfer pump = 2 (on) or any electric fuel pump = 1 (on/high; 2 = auto is not lit) |
| PARKING BRAKE | `sim/cockpit2/controls/parking_brake_ratio` (fallback `sim/flightmodel/controls/parkbrake`) | > 0.01 |
| LOW VOLTS | `sim/cockpit2/annunciators/low_voltage` | != 0 |
| DOOR | `sim/flightmodel2/misc/canopy_open_ratio`, `door_open_ratio[0..19]`, `sim/cockpit2/annunciators/cabin_door_open` | any > 0.01 / != 0 |

The logic is based on dpeukert's
[honeycomb-xplane-linux](https://gitlab.com/dpeukert/honeycomb-xplane-linux)
and jorgeuvo's [Honeycomb-Bravo-Plugin](https://github.com/jorgeuvo/Honeycomb-Bravo-Plugin).
Add-on aircraft with custom systems (Zibo 737, FlightFactor, etc.) may not
drive these default datarefs, so some lights won't follow them.

## Checking it is running

Look in `~/X-Plane 12/Log.txt`:

    grep HoneycombBravoLights ~/"X-Plane 12/Log.txt"

You should see `version ... started`, `found all 28 Bravo LEDs`, and
`aircraft ICAO '...'`. If you see `no Bravo LEDs`, the driver isn't loaded
or the Bravo is unplugged (`ls /sys/class/leds | grep bravo`). If you see
`permission denied`, the udev rule or `plugdev` membership is missing.
You can also see the plugin in X-Plane under Plugins > Plugin Admin.

## Rebuilding

The X-Plane SDK is not included. Download the official headers once into
`SDK/` (written against SDK 4.1.1; newer zips from the same page also ship
`CHeaders/XPLM`). From this directory (`xplane-plugin/`):

    ./fetch-sdk.sh              # or: curl + unzip, see the repository README
    XPSDK=SDK ./build.sh        # or: make XPSDK=SDK
    install -m 755 build/HoneycombBravoLights/lin_x64/HoneycombBravoLights.xpl \
        ~/"X-Plane 12/Resources/plugins/HoneycombBravoLights/lin_x64/"

`SDK/` is the unpacked X-Plane SDK (`CHeaders/XPLM`). The plugin doesn't link
against XPLM; X-Plane resolves those symbols when it loads the plugin.
Tunables such as `FIXED_GEAR_SHOW_GREEN`, `ANTI_ICE_LIT_WHEN_OFF` and
`BUS_VOLTS_MIN` are at the top of `src/HoneycombBravoLights.c`.

## Removing

Quit X-Plane, then delete the plugin folder:

    rm -rf ~/"X-Plane 12/Resources/plugins/HoneycombBravoLights"

(`xplane-plugin/` in this repository is only the source.)
