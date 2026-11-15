#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Event-driven display & power manager for Samsung Galaxy S22 (S6E3FAC):
- Applies /etc/s22-display.conf at boot & reloads on file change via Gio FileMonitor (zero polling)
- Keeps Always-On Display (AOD) OFF by default so the AMOLED & 3 rails shut down 100% on blank
- Wakes the screen for NOTIFICATION_WAKE_SEC (default 3 s) on incoming SMS / notifications,
  then turns it 100% off again unless the user touches the screen
- Switches panel video_mode (120 Hz -> 60 Hz) during active MPRIS video playback
- Claims STK33915 ALS / Proximity via iio-sensor-proxy ONLY while needed (0% idle wakeups)
"""

import glob
import json
import os
import pwd
import subprocess
import time
from pathlib import Path

import gi
gi.require_version("Gio", "2.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gio, GLib

CONF_PATH = Path("/etc/s22-display.conf")
STATE_JSON = Path("/run/s22-display-state.json")


def find_panel() -> Path | None:
    for p in sorted(Path("/sys/bus/mipi-dsi/devices").glob("*")):
        if (p / "vrr_mode").exists():
            return p
    return None


def find_dsi_enabled() -> Path | None:
    for p in sorted(Path("/sys/class/drm").glob("card*-DSI-1/enabled")):
        return p
    return None


def write_sysfs(path: Path, val: str) -> None:
    try:
        path.write_text(f"{val}\n")
    except OSError:
        pass


def read_sysfs(path: Path, default: str = "") -> str:
    try:
        return path.read_text().strip()
    except OSError:
        return default


class S22DisplayDaemon:
    def __init__(self, panel: Path):
        self.panel = panel
        self.dsi_enabled_path = find_dsi_enabled()
        self.conf: dict[str, str] = {}
        self.sys_bus: Gio.DBusConnection | None = None
        self.user_bus: Gio.DBusConnection | None = None
        self.user_uid: int | None = None
        self.user_name: str | None = None
        self.sensor_proxy: Gio.DBusProxy | None = None
        self.als_claimed = False
        self.prox_claimed = False
        self.wake_timer_id = 0
        self.wake_touch_Baseline = 0.0
        self.touch_watches: list[int] = []
        self.touch_fds: list[int] = []
        self.user_touched_during_wake = False
        self.session_poll_id = 0

    def parse_conf(self) -> dict[str, str]:
        cfg = {
            "VRR_MODE": "dynamic",
            "GAME_MODE": "0",
            "COLOUR_MODE": "vivid",
            "WHITE_BALANCE_RGB": "255 255 255",
            "NIGHT_KELVIN": "4300",
            "ACL_MODE": "1",
            "HBM_AUTO": "1",
            "HBM_BOOST_LUX": "15000",
            "HBM_SUNLIGHT_LUX": "40000",
            "HBM_EXIT_LUX": "8000",
            "AOD_ENABLED": "0",
            "AOD_NITS": "10",
            "AOD_POCKET_GUARD": "1",
            "NOTIFICATION_WAKE_SEC": "3",
        }
        if CONF_PATH.exists():
            for raw in CONF_PATH.read_text(errors="ignore").splitlines():
                line = raw.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                cfg[k.strip()] = v.strip().strip('"').strip("'")
        return cfg

    def is_screen_on(self) -> bool:
        if self.dsi_enabled_path and read_sysfs(self.dsi_enabled_path) == "disabled":
            return False
        for bl in Path("/sys/class/backlight").glob("*/bl_power"):
            if read_sysfs(bl, "0") != "0":
                return False
        aod = read_sysfs(self.panel / "aod_mode")
        if "active=1" in aod:
            return False
        return True

    def publish_state(self) -> None:
        data = {
            "aod": int(self.conf.get("AOD_ENABLED", "0") == "1"),
            "aod_nits": int(self.conf.get("AOD_NITS", "10") or 10),
            "game": int(read_sysfs(self.panel / "game_mode", "0") == "1"),
            "vrr": read_sysfs(self.panel / "vrr_mode", "dynamic"),
            "colour": read_sysfs(self.panel / "colour_mode", "vivid"),
            "hbm": int(read_sysfs(self.panel / "hbm_mode", "0") or 0),
            "hbm_auto": int(self.conf.get("HBM_AUTO", "1") == "1"),
            "wake_sec": int(self.conf.get("NOTIFICATION_WAKE_SEC", "3") or 3),
            "kelvin": int(read_sysfs(self.panel / "night_kelvin", "4300") or 4300),
        }
        tmp = Path("/run/s22-display-state.json.tmp")
        try:
            tmp.write_text(json.dumps(data) + "\n")
            tmp.chmod(0o644)
            tmp.replace(STATE_JSON)
        except OSError:
            pass

    def apply_conf(self) -> None:
        self.conf = self.parse_conf()
        game = self.conf.get("GAME_MODE", "0") == "1"
        vrr = "game" if game else self.conf.get("VRR_MODE", "dynamic")
        if vrr == "adaptive":
            vrr = "dynamic"
        write_sysfs(self.panel / "game_mode", "1" if game else "0")
        write_sysfs(self.panel / "vrr_mode", vrr)
        write_sysfs(self.panel / "colour_mode", self.conf.get("COLOUR_MODE", "vivid"))
        write_sysfs(self.panel / "white_balance_rgb", self.conf.get("WHITE_BALANCE_RGB", "255 255 255"))
        write_sysfs(self.panel / "night_kelvin", self.conf.get("NIGHT_KELVIN", "4300"))
        write_sysfs(self.panel / "acl_mode", self.conf.get("ACL_MODE", "1"))
        write_sysfs(self.panel / "aod_brightness", self.conf.get("AOD_NITS", "10"))
        aod_en = "1" if self.conf.get("AOD_ENABLED", "0") == "1" else "0"
        write_sysfs(self.panel / "aod_mode", aod_en)
        if self.conf.get("HBM_AUTO", "1") == "0":
            pass
        self.sync_sensor_claims()
        self.publish_state()

    def sync_sensor_claims(self) -> None:
        if not self.sensor_proxy:
            return
        screen_on = self.is_screen_on()
        want_als = screen_on and (self.conf.get("HBM_AUTO", "1") == "1")
        want_prox = (self.conf.get("AOD_ENABLED", "0") == "1") and (self.conf.get("AOD_POCKET_GUARD", "1") == "1")

        if want_als and not self.als_claimed:
            try:
                self.sensor_proxy.call_sync("ClaimLight", None, Gio.DBusCallFlags.NONE, 1000, None)
                self.als_claimed = True
            except Exception:
                pass
        elif not want_als and self.als_claimed:
            try:
                self.sensor_proxy.call_sync("ReleaseLight", None, Gio.DBusCallFlags.NONE, 1000, None)
            except Exception:
                pass
            self.als_claimed = False
            if read_sysfs(self.panel / "hbm_mode", "0") != "0" and self.conf.get("HBM_AUTO", "1") == "1":
                write_sysfs(self.panel / "hbm_mode", "0")

        if want_prox and not self.prox_claimed:
            try:
                self.sensor_proxy.call_sync("ClaimProximity", None, Gio.DBusCallFlags.NONE, 1000, None)
                self.prox_claimed = True
            except Exception:
                pass
        elif not want_prox and self.prox_claimed:
            try:
                self.sensor_proxy.call_sync("ReleaseProximity", None, Gio.DBusCallFlags.NONE, 1000, None)
            except Exception:
                pass
            self.prox_claimed = False

    def on_sensor_props_changed(self, _proxy, changed_variant, _invalidated) -> None:
        changed = changed_variant.unpack()
        if "LightLevel" in changed and self.conf.get("HBM_AUTO", "1") == "1" and self.is_screen_on():
            lux = float(changed["LightLevel"])
            cur = read_sysfs(self.panel / "hbm_mode", "0")
            sun_lux = float(self.conf.get("HBM_SUNLIGHT_LUX", "40000"))
            boost_lux = float(self.conf.get("HBM_BOOST_LUX", "15000"))
            exit_lux = float(self.conf.get("HBM_EXIT_LUX", "8000"))
            if lux >= sun_lux and cur != "2":
                write_sysfs(self.panel / "hbm_dbv", "4095")
                write_sysfs(self.panel / "hbm_mode", "2")
            elif boost_lux <= lux < sun_lux and cur == "0":
                write_sysfs(self.panel / "hbm_mode", "1")
            elif lux <= exit_lux and cur != "0":
                write_sysfs(self.panel / "hbm_mode", "0")

        if "ProximityNear" in changed and self.conf.get("AOD_ENABLED", "0") == "1":
            if self.conf.get("AOD_POCKET_GUARD", "1") == "1":
                near = bool(changed["ProximityNear"])
                write_sysfs(self.panel / "aod_mode", "0" if near else "1")

    def run_dpms(self, on: bool) -> None:
        for rt in sorted(Path("/run/user").glob("*")):
            wsocks = [s for s in sorted(rt.glob("wayland-[0-9]")) if s.is_socket()]
            if not wsocks:
                continue
            try:
                uid = int(rt.name)
                uname = pwd.getpwuid(uid).pw_name
            except (ValueError, KeyError):
                continue
            env = {
                "PATH": "/usr/local/bin:/usr/bin:/bin",
                "XDG_RUNTIME_DIR": str(rt),
                "WAYLAND_DISPLAY": wsocks[0].name,
            }
            if (rt / "bus").is_socket():
                env["DBUS_SESSION_BUS_ADDRESS"] = f"unix:path={rt}/bus"
            subprocess.Popen(
                ["sudo", "-u", uname, "env", *(f"{k}={v}" for k, v in env.items()),
                 "kscreen-doctor", "--dpms", "on" if on else "off"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return

    def stop_touch_watchers(self) -> None:
        for wid in self.touch_watches:
            GLib.source_remove(wid)
        self.touch_watches.clear()
        for fd in self.touch_fds:
            try:
                os.close(fd)
            except OSError:
                pass
        self.touch_fds.clear()

    def start_touch_watchers(self) -> None:
        self.stop_touch_watchers()
        for ev in glob.glob("/dev/input/event*"):
            name_p = Path(f"/sys/class/input/{Path(ev).name}/device/name")
            dev_name = read_sysfs(name_p).lower()
            if not any(k in dev_name for k in ("fts", "touch", "keys", "pwrkey")):
                continue
            try:
                fd = os.open(ev, os.O_RDONLY | os.O_NONBLOCK)
            except OSError:
                continue
            self.touch_fds.append(fd)

            def _on_input(source_fd, _cond):
                try:
                    os.read(source_fd, 512)
                except OSError:
                    pass
                self.user_touched_during_wake = True
                return True

            wid = GLib.io_add_watch(fd, GLib.IO_IN, _on_input)
            self.touch_watches.append(wid)

    def _on_wake_timeout(self) -> bool:
        self.wake_timer_id = 0
        touched = self.user_touched_during_wake
        self.stop_touch_watchers()
        if not touched:
            self.run_dpms(False)
        return False

    def trigger_notification_wake(self) -> None:
        wake_sec = int(self.conf.get("NOTIFICATION_WAKE_SEC", "3") or 0)
        if wake_sec <= 0:
            return
        if self.is_screen_on():
            return
        self.user_touched_during_wake = False
        self.start_touch_watchers()
        self.run_dpms(True)
        if self.wake_timer_id:
            GLib.source_remove(self.wake_timer_id)
        self.wake_timer_id = GLib.timeout_add_seconds(wake_sec, self._on_wake_timeout)

    def on_modem_sms_added(self, _conn, _sender, _path, _iface, _sig, params) -> None:
        try:
            _sms_path, received = params.unpack()
            if received:
                self.trigger_notification_wake()
        except Exception:
            self.trigger_notification_wake()

    def connect_user_session_bus(self) -> bool:
        if self.user_bus and not self.user_bus.is_closed():
            return True
        for rt in sorted(Path("/run/user").glob("*")):
            bus_sock = rt / "bus"
            if not bus_sock.is_socket():
                continue
            try:
                uid = int(rt.name)
                if uid < 1000:
                    continue
                addr = f"unix:path={bus_sock}"
                # Connect cleanly via seteuid to user's DBus socket
                orig_euid = os.geteuid()
                orig_egid = os.getegid()
                pw = pwd.getpwuid(uid)
                try:
                    os.setegid(pw.pw_gid)
                    os.seteuid(uid)
                    conn = Gio.DBusConnection.new_for_address_sync(
                        addr,
                        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
                        Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
                        None,
                        None,
                    )
                finally:
                    os.seteuid(orig_euid)
                    os.setegid(orig_egid)

                self.user_bus = conn
                self.user_uid = uid
                self.user_name = pw.pw_name

                # 1. Subscribe to MPRIS playback changes (video/media -> 60Hz)
                conn.signal_subscribe(
                    None,
                    "org.freedesktop.DBus.Properties",
                    "PropertiesChanged",
                    "/org/mpris/MediaPlayer2",
                    "org.mpris.MediaPlayer2.Player",
                    Gio.DBusSignalFlags.NONE,
                    self.on_mpris_changed,
                )
                # 2. Subscribe to KDE ScreenSaver ActiveChanged to release/claim ALS on blank/unblank
                conn.signal_subscribe(
                    None,
                    "org.freedesktop.ScreenSaver",
                    "ActiveChanged",
                    "/ScreenSaver",
                    None,
                    Gio.DBusSignalFlags.NONE,
                    lambda *_: GLib.timeout_add_seconds(2, self._deferred_sensor_sync),
                )
                # 3. Monitor Notifications Notify calls via BecomeMonitor on a dedicated monitor connection
                self._start_notification_monitor(addr, uid, pw.pw_gid)
                return False
            except Exception:
                continue
        return True

    def _deferred_sensor_sync(self) -> bool:
        self.sync_sensor_claims()
        return False

    def _start_notification_monitor(self, addr: str, uid: int, gid: int) -> None:
        orig_euid = os.geteuid()
        orig_egid = os.getegid()
        try:
            os.setegid(gid)
            os.seteuid(uid)
            mon = Gio.DBusConnection.new_for_address_sync(
                addr,
                Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
                Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
                None,
                None,
            )
        except Exception:
            return
        finally:
            os.seteuid(orig_euid)
            os.setegid(orig_egid)

        def _filter(_conn, msg, _incoming):
            if (msg.get_message_type() == Gio.DBusMessageType.METHOD_CALL and
                    msg.get_interface() == "org.freedesktop.Notifications" and
                    msg.get_member() == "Notify"):
                # Ignore silent/transient OSD brightness/volume updates
                body = msg.get_body()
                app_name = ""
                if body is not None:
                    try:
                        app_name = str(body.get_child_value(0).get_string()).lower()
                    except Exception:
                        pass
                if app_name not in ("plasmashell", "powerdevil", "kwin", "s22-display"):
                    GLib.idle_add(self.trigger_notification_wake)
            return None

        mon.add_filter(_filter)
        try:
            mon.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus.Monitoring",
                "BecomeMonitor",
                GLib.Variant("(asu)", (["interface='org.freedesktop.Notifications',member='Notify'"], 0)),
                None,
                Gio.DBusCallFlags.NONE,
                2000,
                None,
            )
            self._notif_mon = mon
        except Exception:
            pass

    def on_mpris_changed(self, _conn, _sender, _path, _iface, _sig, params) -> None:
        try:
            _iface_name, changed, _inv = params.unpack()
            status = changed.get("PlaybackStatus")
            if status == "Playing":
                write_sysfs(self.panel / "video_mode", "1")
            elif status in ("Paused", "Stopped"):
                write_sysfs(self.panel / "video_mode", "0")
        except Exception:
            pass

    def setup_dbus(self) -> None:
        self.sys_bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)

        # Subscribe to ModemManager incoming SMS (works even before user session unlocks!)
        self.sys_bus.signal_subscribe(
            "org.freedesktop.ModemManager1",
            "org.freedesktop.ModemManager1.Modem.Messaging",
            "Added",
            None,
            None,
            Gio.DBusSignalFlags.NONE,
            self.on_modem_sms_added,
        )

        # Watch DRM hotplug / DPMS changes via udev netlink? Also watch SensorProxy on System Bus:
        try:
            self.sensor_proxy = Gio.DBusProxy.new_sync(
                self.sys_bus,
                Gio.DBusProxyFlags.DO_NOT_AUTO_START_AT_CONSTRUCTION,
                None,
                "net.hadess.SensorProxy",
                "/net/hadess/SensorProxy",
                "net.hadess.SensorProxy",
                None,
            )
            self.sensor_proxy.connect("g-properties-changed", self.on_sensor_props_changed)
        except Exception:
            self.sensor_proxy = None

        # Connect to user session bus (now or once Plasma Mobile starts)
        if self.connect_user_session_bus():
            self.session_poll_id = GLib.timeout_add_seconds(5, self.connect_user_session_bus)

    def setup_conf_monitor(self) -> None:
        gfile = Gio.File.new_for_path(str(CONF_PATH))
        self._conf_mon = gfile.monitor_file(Gio.FileMonitorFlags.NONE, None)
        self._conf_mon.connect(
            "changed",
            lambda _m, _f, _of, ev: self.apply_conf()
            if ev in (Gio.FileMonitorEvent.CHANGES_DONE_HINT, Gio.FileMonitorEvent.CREATED)
            else None,
        )


def main() -> int:
    panel = None
    for _ in range(30):
        panel = find_panel()
        if panel is not None:
            break
        time.sleep(1)
    if panel is None:
        return 1

    daemon = S22DisplayDaemon(panel)
    daemon.setup_dbus()
    daemon.apply_conf()
    daemon.setup_conf_monitor()

    loop = GLib.MainLoop()
    loop.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
