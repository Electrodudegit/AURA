#!/usr/bin/env python3
"""Local AURA companion. Sends CPU, RAM, and a stub title to one paired device.

This process opens a single TCP connection to the AURA you name. It does not
open any other socket, does not read your real notification center, and does
not upload anything. The pairing file remembers one device; a second AURA is
refused until you pass --rebind.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

PORT = 42131
STUBS = ("", "", "Companion linked", "", "Build finished", "")


def pair_path(arg):
    if arg:
        return os.path.expanduser(arg)
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
        return os.path.join(base, "aura-companion", "pair.json")
    return os.path.join(os.path.expanduser("~"), ".config", "aura-companion", "pair.json")


def load_pair(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    if not isinstance(data, dict):
        raise SystemExit("pair file is not a JSON object")
    return data


def save_pair(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
        os.chmod(tmp, 0o600)
    os.replace(tmp, path)
    os.chmod(path, 0o600)


def cpu_ram_linux(prev):
    with open("/proc/stat", "r", encoding="ascii") as f:
        parts = f.readline().split()
    nums = [int(x) for x in parts[1:]]
    idle = nums[3] + (nums[4] if len(nums) > 4 else 0)
    total = sum(nums)
    cpu = 0
    if prev is not None:
        di = idle - prev[0]
        dt = total - prev[1]
        if dt > 0:
            cpu = 100 - (100 * di // dt)
            cpu = max(0, min(100, cpu))
    info = {}
    with open("/proc/meminfo", "r", encoding="ascii") as f:
        for line in f:
            k, v = line.split(":", 1)
            info[k] = int(v.strip().split()[0])
    total_kb = info.get("MemTotal", 1)
    avail = info.get("MemAvailable", info.get("MemFree", 0))
    ram = int(100 * (total_kb - avail) / total_kb) if total_kb else 0
    return max(0, min(100, ram)), cpu, (idle, total)


def cpu_ram_darwin():
    ncpu = os.cpu_count() or 1
    out = subprocess.check_output(["ps", "-A", "-o", "%cpu="], text=True)
    busy = 0.0
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            busy += float(line)
        except ValueError:
            pass
    cpu = max(0, min(100, int(busy / ncpu)))
    pagesize = int(subprocess.check_output(["sysctl", "-n", "hw.pagesize"]))
    mem = int(subprocess.check_output(["sysctl", "-n", "hw.memsize"]))
    vm = subprocess.check_output(["vm_stat"], text=True)
    pages = {}
    for line in vm.splitlines():
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        v = v.strip().strip(".")
        try:
            pages[k] = int(v)
        except ValueError:
            pass
    free = pages.get("Pages free", 0) + pages.get("Pages speculative", 0) + pages.get("Pages inactive", 0)
    used = max(0, mem - free * pagesize)
    ram = int(100 * used / mem) if mem else 0
    return max(0, min(100, ram)), cpu, None


def cpu_ram_windows(prev):
    import ctypes

    class FILETIME(ctypes.Structure):
        _fields_ = [("lo", ctypes.c_uint32), ("hi", ctypes.c_uint32)]

    def ft(x):
        return (x.hi << 32) | x.lo

    idle, kernel, user = FILETIME(), FILETIME(), FILETIME()
    ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user))
    it, kt, ut = ft(idle), ft(kernel), ft(user)
    cpu = 0
    if prev is not None:
        di = it - prev[0]
        total = (kt - prev[1]) + (ut - prev[2])
        if total > 0:
            cpu = max(0, min(100, int(100 * (total - di) / total)))

    class MEM(ctypes.Structure):
        _fields_ = [
            ("dwLength", ctypes.c_ulong),
            ("dwMemoryLoad", ctypes.c_ulong),
            ("ullTotalPhys", ctypes.c_ulonglong),
            ("ullAvailPhys", ctypes.c_ulonglong),
            ("ullTotalPageFile", ctypes.c_ulonglong),
            ("ullAvailPageFile", ctypes.c_ulonglong),
            ("ullTotalVirtual", ctypes.c_ulonglong),
            ("ullAvailVirtual", ctypes.c_ulonglong),
            ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
        ]

    mem = MEM()
    mem.dwLength = ctypes.sizeof(MEM)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(mem))
    ram = max(0, min(100, int(mem.dwMemoryLoad)))
    return ram, cpu, (it, kt, ut)


def sample(prev):
    if os.path.exists("/proc/stat"):
        ram, cpu, prev = cpu_ram_linux(prev)
        return cpu, ram, prev
    if sys.platform == "darwin":
        ram, cpu, prev = cpu_ram_darwin()
        return cpu, ram, prev
    if sys.platform == "win32":
        ram, cpu, prev = cpu_ram_windows(prev)
        return cpu, ram, prev
    raise SystemExit("This companion reads /proc, macOS sysctl, or Windows GetSystemTimes. No other source is used.")


def clean_now(spec):
    """'Title|Artist' -> NOW line. Each field is printable ASCII, max 47."""
    if spec.count("|") != 1:
        raise SystemExit('--now needs exactly one "|", e.g. --now "Song|Artist"')
    title, artist = spec.split("|")
    return "NOW %s|%s" % (clean_note(title), clean_note(artist))


def windows_spotify_now():
    """Spotify window title is usually 'Song - Artist'. Local only; no Spotify API."""
    try:
        out = subprocess.check_output(
            ["powershell", "-NoProfile", "-Command",
             "(Get-Process -Name Spotify -ErrorAction SilentlyContinue | "
             "Where-Object { $_.MainWindowTitle } | Select-Object -First 1 "
             "-ExpandProperty MainWindowTitle)"],
            text=True, timeout=2, stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.SubprocessError):
        return None
    if not out or out.lower().startswith("spotify"):
        return None
    if " - " not in out:
        return clean_note(out) + "|"
    title, artist = out.split(" - ", 1)
    return "%s|%s" % (clean_note(title), clean_note(artist))


def load_art(spec):
    """'WxH:path' raw RGB565 little-endian, row-major. nbytes must be w*h*2, dims <= 48."""
    dim, path = spec.split(":", 1)
    w_s, h_s = dim.lower().split("x", 1)
    w, h = int(w_s), int(h_s)
    blob = open(os.path.expanduser(path), "rb").read()
    if w < 1 or h < 1 or w > 48 or h > 48 or len(blob) != w * h * 2:
        raise SystemExit("--art needs 1..48 and a raw RGB565 file of w*h*2 bytes")
    return w, h, blob


def stub_note(now, override):
    if override is not None:
        return override
    return STUBS[(int(now) // 20) % len(STUBS)]


def clean_note(note):
    out = []
    for ch in note:
        o = ord(ch)
        if 0x20 <= o <= 0x7E:
            out.append(ch)
        if len(out) >= 47:
            break
    return "".join(out)


def read_line(sock):
    buf = b""
    while b"\n" not in buf:
        chunk = sock.recv(1)
        if not chunk:
            raise ConnectionError("AURA closed the connection")
        buf += chunk
        if len(buf) > 200:
            raise ConnectionError("reply too long")
    return buf.decode("ascii").strip("\r\n")


def main():
    p = argparse.ArgumentParser(description="Share CPU/RAM with one paired AURA over the local network.")
    p.add_argument("--host", help="AURA address on this LAN (shown on the companion page)")
    p.add_argument("--port", type=int, default=PORT)
    p.add_argument("--token", help="8-character code shown on the AURA")
    p.add_argument("--note", help="replace the stub title (still local, max 47 ASCII characters)")
    p.add_argument("--now", help='send Now Playing: "Title|Artist" ("|" clears). On Windows, Spotify window title is used when this is omitted.')
    p.add_argument("--art", help='optional cover: "WxH:file" raw RGB565 LE, max 48x48, sent once after pairing')
    p.add_argument("--pair-file", help="default: %%APPDATA%%/aura-companion/pair.json on Windows, else ~/.config/aura-companion/pair.json")
    p.add_argument("--rebind", action="store_true", help="replace the saved device with --host and --token")
    p.add_argument("--interval", type=float, default=2.0)
    args = p.parse_args()
    if args.interval < 0.5:
        args.interval = 0.5

    path = pair_path(args.pair_file)
    saved = load_pair(path)
    if saved and not args.rebind:
        host = saved.get("host")
        token = saved.get("token")
        client_id = saved.get("client_id")
        port = int(saved.get("port", PORT))
        if args.host and args.host != host:
            raise SystemExit("pair file is already bound to %s. Pass --rebind to switch devices." % host)
        if args.token and args.token != token:
            raise SystemExit("pair file already has a token. Pass --rebind --token to replace it.")
    else:
        host = args.host or (saved or {}).get("host")
        token = args.token or (saved or {}).get("token")
        client_id = (saved or {}).get("client_id")
        port = args.port
        if not host or not token:
            raise SystemExit("first run needs --host and --token (the code on the AURA companion page)")
    token = token.strip().upper()
    if len(token) != 8:
        raise SystemExit("token must be the 8-character code on the device")
    if not client_id or len(client_id) != 16:
        client_id = os.urandom(8).hex()
    save_pair(path, {"host": host, "port": port, "token": token, "client_id": client_id})

    print("AURA companion: local only. CPU, RAM, and a stub title go to %s:%s and nowhere else." % (host, port))
    print("Bound to this one device. Pair file: %s" % path)
    prev = None
    while True:
        try:
            sock = socket.create_connection((host, port), timeout=5)
        except OSError as e:
            print("waiting for AURA (%s)" % e)
            time.sleep(2)
            continue
        sock.settimeout(5)
        try:
            sock.sendall(("HELLO %s %s\n" % (token, client_id)).encode("ascii"))
            reply = read_line(sock)
            if reply != "OK":
                print("AURA refused the pair (%s). On the device, forget the pair and run again with --rebind --token <new code>." % reply)
                return 1
            print("paired")
            if args.art:
                aw, ah, px = load_art(args.art)
                sock.sendall(("ART %d %d %d\n" % (aw, ah, len(px))).encode("ascii") + px)
                ack = read_line(sock)
                if ack != "ACK":
                    print("AURA refused ART (%s)" % ack)
                    break
            last_now = None
            if args.now is not None:
                sock.sendall((clean_now(args.now) + "\n").encode("ascii"))
                ack = read_line(sock)
                if ack != "ACK":
                    print("AURA refused NOW (%s)" % ack)
                    break
                last_now = args.now
            while True:
                cpu, ram, prev = sample(prev)
                note = clean_note(stub_note(time.time(), args.note))
                line = "STAT %d %d" % (cpu, ram)
                if note:
                    line += " " + note
                sock.sendall((line + "\n").encode("ascii"))
                ack = read_line(sock)
                if ack != "ACK":
                    print("unexpected reply %s" % ack)
                    break
                if args.now is None and sys.platform == "win32":
                    spec = windows_spotify_now()
                    if spec and spec != last_now:
                        sock.sendall((clean_now(spec) + "\n").encode("ascii"))
                        ack = read_line(sock)
                        if ack != "ACK":
                            print("AURA refused NOW (%s)" % ack)
                            break
                        last_now = spec
                time.sleep(args.interval)
        except (OSError, ConnectionError, UnicodeError) as e:
            print("link dropped (%s)" % e)
        finally:
            try:
                sock.close()
            except OSError:
                pass
        time.sleep(2)


if __name__ == "__main__":
    sys.exit(main() or 0)
