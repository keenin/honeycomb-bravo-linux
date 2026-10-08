/*
 * HoneycombBravoLights - X-Plane 12 plugin (Linux) that drives the 28 lights
 * of the Honeycomb Bravo throttle quadrant from sim state.
 *
 * It talks to the hid-honeycomb-bravo kernel driver, which exposes each light
 * as an LED class device: /sys/class/leds/bravo:<color>:<name>/brightness.
 * The plugin writes "0"/"1" to those files only when a light changes.
 *
 * Dataref logic follows dpeukert/honeycomb-xplane-linux (HoneycombBravoHelper.lua)
 * and jorgeuvo/Honeycomb-Bravo-Plugin, using X-Plane 12 default sim/ datarefs.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#define PLUGIN_NAME  "HoneycombBravoLights"
#define PLUGIN_SIG   "keenin.honeycomb.bravo.lights"
#define PLUGIN_VER   "0.1.0"

/* ---- tunables ------------------------------------------------------------ */
#define LOOP_INTERVAL_S       0.1f  /* ~10 Hz */
#define DEVICE_CHECK_TICKS    20    /* re-check / reopen LEDs every ~2 s */
#define BUS_VOLTS_MIN         1.0f  /* lights only when some bus has power */
/* Fixed (non-retractable) gear: 1 = show green when the leg is down (as in
 * dpeukert's script), 0 = keep gear lights dark (as in jorgeuvo's plugin). */
#define FIXED_GEAR_SHOW_GREEN 1
/* ANTI ICE light: 0 = lit while pilot pitot heat is ON (dpeukert),
 * 1 = lit while pitot heat is OFF, i.e. a warning (jorgeuvo). */
#define ANTI_ICE_LIT_WHEN_OFF 0

#ifndef LED_SYSFS_DIR
#define LED_SYSFS_DIR "/sys/class/leds/"
#endif

/* ---- LEDs ---------------------------------------------------------------- */
enum {
	L_AP_HDG, L_AP_NAV, L_AP_APR, L_AP_REV, L_AP_ALT, L_AP_VS, L_AP_IAS, L_AP_AP,
	L_GEAR_L_G, L_GEAR_L_R, L_GEAR_N_G, L_GEAR_N_R, L_GEAR_R_G, L_GEAR_R_R,
	L_MASTER_WARN, L_ENG_FIRE, L_OIL, L_FUEL, L_ANTI_ICE, L_STARTER, L_APU,
	L_MASTER_CAUT, L_VACUUM, L_HYD, L_AUX_FUEL, L_PARK_BRAKE, L_VOLTS, L_DOOR,
	NUM_LEDS
};

static const char *const led_names[NUM_LEDS] = {
	"bravo::ap_hdg", "bravo::ap_nav", "bravo::ap_apr", "bravo::ap_rev",
	"bravo::ap_alt", "bravo::ap_vs", "bravo::ap_ias", "bravo::ap_autopilot",
	"bravo:green:gear_left", "bravo:red:gear_left",
	"bravo:green:gear_nose", "bravo:red:gear_nose",
	"bravo:green:gear_right", "bravo:red:gear_right",
	"bravo:red:master_warning", "bravo:red:engine_fire",
	"bravo::low_oil_pressure", "bravo::low_fuel_pressure", "bravo::anti_ice",
	"bravo::starter_engaged", "bravo::apu",
	"bravo:amber:master_caution", "bravo::vacuum", "bravo::low_hyd_pressure",
	"bravo::aux_fuel_pump", "bravo::parking_brake", "bravo::low_volts",
	"bravo::door",
};

static int  led_fd[NUM_LEDS];
static ino_t led_ino[NUM_LEDS];
static signed char led_state[NUM_LEDS];  /* last value written: -1 unknown */
static int  leds_open;                   /* number of LED fds open */
static int  logged_missing;              /* log "not found" only once */
static int  tick;

static void logf_(const char *fmt, ...)
{
	char buf[512];
	int n = snprintf(buf, sizeof(buf), PLUGIN_NAME ": ");
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
	va_end(ap);
	strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
	XPLMDebugString(buf);
}

static void leds_close(void)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		if (led_fd[i] >= 0)
			close(led_fd[i]);
		led_fd[i] = -1;
		led_state[i] = -1;
	}
	leds_open = 0;
}

static void leds_open_all(void)
{
	char path[256];
	int missing = 0, denied = 0;

	leds_open = 0;
	for (int i = 0; i < NUM_LEDS; i++) {
		struct stat st;
		snprintf(path, sizeof(path), LED_SYSFS_DIR "%s/brightness", led_names[i]);
		led_fd[i] = open(path, O_WRONLY | O_CLOEXEC);
		led_state[i] = -1;
		if (led_fd[i] < 0) {
			if (errno == EACCES || errno == EPERM)
				denied++;
			else
				missing++;
			continue;
		}
		led_ino[i] = fstat(led_fd[i], &st) == 0 ? st.st_ino : 0;
		leds_open++;
	}

	if (leds_open == NUM_LEDS) {
		logf_("found all %d Bravo LEDs in " LED_SYSFS_DIR, NUM_LEDS);
		logged_missing = 0;
	} else if (leds_open > 0) {
		logf_("found %d of %d Bravo LEDs (%d missing, %d permission denied); using what exists",
		      leds_open, NUM_LEDS, missing, denied);
		logged_missing = 0;
	} else if (!logged_missing) {
		if (denied)
			logf_("Bravo LEDs exist but cannot be opened (permission denied) - "
			      "check the udev rule / plugdev group. Will keep retrying quietly.");
		else
			logf_("no Bravo LEDs in " LED_SYSFS_DIR " (driver not loaded or Bravo "
			      "unplugged). Will keep retrying quietly.");
		logged_missing = 1;
	}
}

/* Detect unplug/replug: the sysfs node is gone or is a new node. */
static int leds_stale(void)
{
	char path[256];
	for (int i = 0; i < NUM_LEDS; i++) {
		struct stat st;
		snprintf(path, sizeof(path), LED_SYSFS_DIR "%s/brightness", led_names[i]);
		int exists = stat(path, &st) == 0;
		if (led_fd[i] < 0) {
			if (exists)
				return 1;      /* an LED appeared */
		} else if (!exists || st.st_ino != led_ino[i]) {
			return 1;              /* LED vanished or was recreated */
		}
	}
	return 0;
}

static void led_set(int i, int on)
{
	on = on ? 1 : 0;
	if (led_fd[i] < 0 || led_state[i] == on)
		return;
	if (pwrite(led_fd[i], on ? "1" : "0", 1, 0) == 1) {
		led_state[i] = (signed char)on;
	} else {
		/* Device probably went away; reopen at the next device check. */
		if (leds_open)
			logf_("write to %s failed (%s); Bravo unplugged? Will reconnect.",
			      led_names[i], strerror(errno));
		leds_close();
		logged_missing = 1;  /* already reported */
	}
}

static void leds_all(int on)
{
	for (int i = 0; i < NUM_LEDS; i++)
		led_set(i, on);
}

/* ---- datarefs -------------------------------------------------------------- */
static XPLMDataRef find_dr(const char *name)
{
	XPLMDataRef r = XPLMFindDataRef(name);
	if (!r)
		logf_("dataref %s not found; its light stays off", name);
	return r;
}

/* Scalar read that works for int, float or double typed datarefs. */
static float dr_f(XPLMDataRef r)
{
	if (!r)
		return 0.0f;
	XPLMDataTypeID t = XPLMGetDataRefTypes(r);
	if (t & xplmType_Float)
		return XPLMGetDataf(r);
	if (t & xplmType_Double)
		return (float)XPLMGetDatad(r);
	if (t & xplmType_Int)
		return (float)XPLMGetDatai(r);
	if (t & xplmType_FloatArray) {
		float v = 0;
		XPLMGetDatavf(r, &v, 0, 1);
		return v;
	}
	if (t & xplmType_IntArray) {
		int v = 0;
		XPLMGetDatavi(r, &v, 0, 1);
		return (float)v;
	}
	return 0.0f;
}

/* Array read (int or float array) into floats; returns count read. */
static int dr_vf(XPLMDataRef r, float *out, int max)
{
	if (!r)
		return 0;
	XPLMDataTypeID t = XPLMGetDataRefTypes(r);
	int n = 0;
	if (t & xplmType_FloatArray) {
		n = XPLMGetDatavf(r, out, 0, max);
	} else if (t & xplmType_IntArray) {
		int tmp[32];
		if (max > 32)
			max = 32;
		n = XPLMGetDatavi(r, tmp, 0, max);
		for (int i = 0; i < n; i++)
			out[i] = (float)tmp[i];
	} else {
		out[0] = dr_f(r);
		n = 1;
	}
	if (n < 0)
		n = 0;
	if (n > max)
		n = max;
	return n;
}

static float dr_max(XPLMDataRef r, int max)
{
	float v[32];
	int n = dr_vf(r, v, max > 32 ? 32 : max);
	float m = 0.0f;
	for (int i = 0; i < n; i++)
		if (v[i] > m)
			m = v[i];
	return m;
}

static int dr_any_eq(XPLMDataRef r, int max, float val)
{
	float v[32];
	int n = dr_vf(r, v, max > 32 ? 32 : max);
	for (int i = 0; i < n; i++)
		if (v[i] == val)
			return 1;
	return 0;
}

static struct {
	XPLMDataRef bus_volts;
	/* autopilot */
	XPLMDataRef hdg, nav, gpss, apr, rev, alt, vs, spd, athr, servos;
	/* gear */
	XPLMDataRef gear_deploy, gear_retract;
	/* annunciators */
	XPLMDataRef master_warn, fire, oil, fuel, pitot_heat_on, starter, apu;
	XPLMDataRef master_caut, vacuum, hyd, xfer_l, xfer_r, fuel_pump;
	XPLMDataRef park_brake, low_volts, canopy, doors, cabin_door;
	/* aircraft info */
	XPLMDataRef icao, num_engines;
} dr;

static int show_hyd = 1;  /* off for single-engine pistons with no hydraulics */

static void find_datarefs(void)
{
	dr.bus_volts     = find_dr("sim/cockpit2/electrical/bus_volts");

	dr.hdg           = find_dr("sim/cockpit2/autopilot/heading_status");
	dr.nav           = find_dr("sim/cockpit2/autopilot/nav_status");
	dr.gpss          = find_dr("sim/cockpit2/autopilot/gpss_status");
	dr.apr           = find_dr("sim/cockpit2/autopilot/approach_status");
	dr.rev           = find_dr("sim/cockpit2/autopilot/backcourse_status");
	dr.alt           = find_dr("sim/cockpit2/autopilot/altitude_hold_status");
	dr.vs            = find_dr("sim/cockpit2/autopilot/vvi_status");
	dr.spd           = find_dr("sim/cockpit2/autopilot/speed_status");
	dr.athr          = find_dr("sim/cockpit2/autopilot/autothrottle_on");
	dr.servos        = find_dr("sim/cockpit2/autopilot/servos_on");

	dr.gear_deploy   = find_dr("sim/flightmodel2/gear/deploy_ratio");
	dr.gear_retract  = find_dr("sim/aircraft/gear/acf_gear_retract");

	dr.master_warn   = find_dr("sim/cockpit2/annunciators/master_warning");
	dr.fire          = find_dr("sim/cockpit2/annunciators/engine_fires");
	dr.oil           = find_dr("sim/cockpit2/annunciators/oil_pressure_low");
	dr.fuel          = find_dr("sim/cockpit2/annunciators/fuel_pressure_low");
	dr.pitot_heat_on = find_dr("sim/cockpit2/ice/ice_pitot_heat_on_pilot");
	dr.starter       = find_dr("sim/cockpit2/engine/actuators/starter_hit");
	dr.apu           = find_dr("sim/cockpit2/electrical/APU_running");

	dr.master_caut   = find_dr("sim/cockpit2/annunciators/master_caution");
	dr.vacuum        = find_dr("sim/cockpit2/annunciators/low_vacuum");
	dr.hyd           = find_dr("sim/cockpit2/annunciators/hydraulic_pressure");
	dr.xfer_l        = find_dr("sim/cockpit2/fuel/transfer_pump_left");
	dr.xfer_r        = find_dr("sim/cockpit2/fuel/transfer_pump_right");
	dr.fuel_pump     = find_dr("sim/cockpit2/engine/actuators/fuel_pump_on");
	dr.park_brake    = XPLMFindDataRef("sim/cockpit2/controls/parking_brake_ratio");
	if (!dr.park_brake)
		dr.park_brake = find_dr("sim/flightmodel/controls/parkbrake");
	dr.low_volts     = find_dr("sim/cockpit2/annunciators/low_voltage");
	dr.canopy        = find_dr("sim/flightmodel2/misc/canopy_open_ratio");
	dr.doors         = find_dr("sim/flightmodel2/misc/door_open_ratio");
	dr.cabin_door    = find_dr("sim/cockpit2/annunciators/cabin_door_open");

	dr.icao          = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
	dr.num_engines   = XPLMFindDataRef("sim/aircraft/engine/acf_num_engines");
}

/* Hide LOW HYD PRESS on single-engine types without hydraulics (list from
 * jorgeuvo's plugin) - X-Plane reports low hyd pressure on them. */
static void update_aircraft(void)
{
	static const char *const no_hyd[] = {
		"C172", "SR22", "SR20", "S22T", "SR22T", "P28A", "C208",
		"K100", "KODI", "DA40", "RV10", NULL
	};
	char icao[41] = {0};
	if (dr.icao)
		XPLMGetDatab(dr.icao, icao, 0, sizeof(icao) - 1);
	show_hyd = 1;
	for (int i = 0; no_hyd[i]; i++)
		if (strcmp(icao, no_hyd[i]) == 0)
			show_hyd = 0;
	logf_("aircraft ICAO '%s'%s", icao,
	      show_hyd ? "" : " (LOW HYD PRESS light disabled for this type)");
}

/* ---- main logic ------------------------------------------------------------ */
static void update_leds(void)
{
	int powered = dr_max(dr.bus_volts, 6) > BUS_VOLTS_MIN;
	if (!powered) {
		leds_all(0);
		return;
	}

	/* Autopilot: 0=off, 1=armed, 2=captured. Armed or captured lights the mode. */
	led_set(L_AP_HDG, dr_f(dr.hdg) >= 1);
	led_set(L_AP_NAV, dr_f(dr.nav) >= 1 || dr_f(dr.gpss) >= 1);
	led_set(L_AP_APR, dr_f(dr.apr) >= 1);
	led_set(L_AP_REV, dr_f(dr.rev) >= 1);
	led_set(L_AP_ALT, dr_f(dr.alt) >= 2);   /* captured only, like references */
	led_set(L_AP_VS,  dr_f(dr.vs) >= 1);
	led_set(L_AP_IAS, dr_f(dr.spd) >= 1 || dr_f(dr.athr) >= 1);
	led_set(L_AP_AP,  dr_f(dr.servos) >= 1);

	/* Gear: index 0 = nose, 1 = left, 2 = right. */
	{
		static const int green[3] = { L_GEAR_N_G, L_GEAR_L_G, L_GEAR_R_G };
		static const int red[3]   = { L_GEAR_N_R, L_GEAR_L_R, L_GEAR_R_R };
		float g[3] = { 0, 0, 0 };
		int n = dr_vf(dr.gear_deploy, g, 3);
		int retract = dr.gear_retract ? dr_f(dr.gear_retract) != 0 : 1;
		for (int i = 0; i < 3; i++) {
			float r = i < n ? g[i] : 0.0f;
			int is_down = r >= 0.999f;
			int moving  = r > 0.001f && !is_down;
			if (!retract) {
				led_set(green[i], FIXED_GEAR_SHOW_GREEN && is_down);
				led_set(red[i], 0);
			} else {
				led_set(green[i], is_down);
				led_set(red[i], moving);
			}
		}
	}

	led_set(L_MASTER_WARN, dr_f(dr.master_warn) != 0);
	led_set(L_ENG_FIRE,    dr_any_eq(dr.fire, 8, 1));
	led_set(L_OIL,         dr_any_eq(dr.oil, 8, 1));
	led_set(L_FUEL,        dr_any_eq(dr.fuel, 8, 1));
	{
		int heat_on = dr_f(dr.pitot_heat_on) != 0;
		led_set(L_ANTI_ICE, ANTI_ICE_LIT_WHEN_OFF ? !heat_on : heat_on);
	}
	led_set(L_STARTER,     dr_any_eq(dr.starter, 8, 1));
	led_set(L_APU,         dr_f(dr.apu) != 0);

	led_set(L_MASTER_CAUT, dr_f(dr.master_caut) != 0);
	led_set(L_VACUUM,      dr_f(dr.vacuum) != 0);
	led_set(L_HYD,         show_hyd && dr_f(dr.hyd) != 0);
	{
		/* Transfer pumps forced on (2), or any electric engine fuel pump ON (1;
		 * 2 = auto is not lit). */
		int engines = dr.num_engines ? (int)dr_f(dr.num_engines) : 8;
		if (engines < 1 || engines > 8)
			engines = 8;
		int aux = dr_f(dr.xfer_l) == 2 || dr_f(dr.xfer_r) == 2 ||
			  dr_any_eq(dr.fuel_pump, engines, 1);
		led_set(L_AUX_FUEL, aux);
	}
	led_set(L_PARK_BRAKE,  dr_f(dr.park_brake) > 0.01f);
	led_set(L_VOLTS,       dr_f(dr.low_volts) != 0);
	led_set(L_DOOR,        dr_f(dr.canopy) > 0.01f ||
			       dr_max(dr.doors, 20) > 0.01f ||
			       dr_f(dr.cabin_door) != 0);
}

static float flight_loop(float since_call, float since_loop, int counter, void *ref)
{
	(void)since_call; (void)since_loop; (void)counter; (void)ref;

	if (++tick >= DEVICE_CHECK_TICKS) {
		tick = 0;
		if (leds_stale()) {
			int had = leds_open;
			leds_close();
			leds_open_all();
			if (had && !leds_open)
				logf_("Bravo LEDs disappeared; will reconnect when it is back.");
		}
	}
	if (leds_open)
		update_leds();
	return LOOP_INTERVAL_S;
}

/* ---- plugin entry points -------------------------------------------------- */
PLUGIN_API int XPluginStart(char *name, char *sig, char *desc)
{
	strcpy(name, PLUGIN_NAME);
	strcpy(sig, PLUGIN_SIG);
	strcpy(desc, "Drives the Honeycomb Bravo lights via the Linux hid-honeycomb-bravo LED driver.");
	for (int i = 0; i < NUM_LEDS; i++) {
		led_fd[i] = -1;
		led_state[i] = -1;
	}
	find_datarefs();
	logf_("version " PLUGIN_VER " started");
	return 1;
}

PLUGIN_API void XPluginStop(void)
{
	leds_close();
	logf_("stopped");
}

PLUGIN_API int XPluginEnable(void)
{
	logged_missing = 0;
	leds_open_all();
	leds_all(0);
	update_aircraft();
	tick = 0;
	XPLMRegisterFlightLoopCallback(flight_loop, LOOP_INTERVAL_S, NULL);
	return 1;
}

PLUGIN_API void XPluginDisable(void)
{
	XPLMUnregisterFlightLoopCallback(flight_loop, NULL);
	leds_all(0);
	leds_close();
	logf_("disabled, lights off");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int msg, void *param)
{
	(void)from;
	if (msg == XPLM_MSG_PLANE_LOADED && (intptr_t)param == 0)
		update_aircraft();
}
