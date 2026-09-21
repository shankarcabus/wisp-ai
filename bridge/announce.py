"""
Announces the bridge on the local network under names the BRIDGE controls.

WHY THIS EXISTS
---------------
The board used to find the Mac by resolving its hostname
(`Marcios-MacBook-Pro-6.local`). That broke: macOS derives LocalHostName from
ComputerName and appends a numeric suffix whenever it detects a conflict on
the network. On this Mac, with many interfaces announcing themselves (anpi0-2,
en1, en2, en4-7), that had already happened 7 times. When it became "-7", the
name written into NVS stopped existing and the firmware sat stuck on
"connecting" — WiFi up, bridge up, and still orphaned.

Pinning LocalHostName would fix it today and break again on the next conflict,
besides touching system configuration. Here the board looks for names that are
born and die with the bridge:

    _wisp._tcp        the service; new firmware looks for this and depends on
                      no hostname at all
    wisp.local        our own fixed name, for provisioning from here on
    <LEGACY>.local    the old hostname, republished as an alias — this is what
                      brings an already-flashed board back without a reflash

All through `dns-sd`, which already ships with macOS. Nothing to install,
nothing to configure.

The IP is rechecked periodically: if DHCP moves the Mac's address, the records
are rebuilt on their own (which is exactly what happened: .106 -> .142).

TWO BRIDGES ON ONE NETWORK
--------------------------
The names above used to be the SAME on every install — `Wisp Bridge` and
`wisp.local`, written here as constants. Two people running the project on one
WiFi was therefore a collision by construction: Bonjour renamed the second one
to `Wisp Bridge (2)`, both published `wisp.local`, and the board — which keeps
the first `_wisp._tcp` answer that carries an address — went to whichever Mac
replied first. Measured at home on 21 Sep 2026: the board ended up on the other
Mac, got 401 because the token is not shared, and the screen said nothing more
useful than "bridge offline".

So each bridge now publishes an identity:

    instance name     `Wisp Bridge (<this machine>)`, so a human browsing the
                      network can tell whose is whose
    TXT id=<8 hex>    SHA-256 of the token, truncated — what the BOARD matches
                      against, because the token is already the one secret the
                      pair shares
    wisp-<id>.local   a host name that is this bridge's alone

`wisp.local` stays published for boards provisioned before this. It is the one
name here that can still collide, and it is the fallback path, not the one the
firmware prefers.

The id is a hash, never the token: it travels in clear text on the network, and
it only has to be stable and unique, not secret.
"""


# Lazy annotations: lets the module run on the system Python 3.9
# (/usr/bin/python3), which never changes and does not depend on asdf. Without
# this, `str | None` is evaluated at definition time and blows up on 3.9.
from __future__ import annotations

import atexit
import hashlib
import shutil
import socket
import subprocess
import sys
import threading
import time

SERVICE = "_wisp._tcp"
INSTANCE = "Wisp Bridge"

# Our own name, for provisioning from here on. Shared by every install, so it
# is the one record here that two bridges can still fight over — see the module
# docstring. Kept for boards provisioned before the per-bridge name existed.
FIXED_NAME = "wisp.local"


def _fingerprint() -> str:
    """
    This bridge's public identity: the first 4 bytes of SHA-256(token), in hex.

    Derived from the token because the token is already exactly what makes a
    board and a bridge a pair — no second secret, nothing new in the config,
    and it survives the IP and the machine name changing. A truncated hash
    identifies; it does not reveal. Empty when there is no token yet, and then
    the board falls back to the old "first one that answers".
    """
    try:
        import config
        token = config.read().get("token") or ""
    except Exception:
        token = ""
    if not token:
        return ""
    return hashlib.sha256(token.encode("utf-8")).hexdigest()[:8]


def _machine() -> str:
    """This Mac's short name, for the instance a human reads in `dns-sd -B`."""
    name = socket.gethostname() or "mac"
    for suffix in (".local.", ".local"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
            break
    return name or "mac"


def _legacy() -> list:
    """
    Old hostnames to republish, taken from the user's config.

    This is for people who already have a board flashed with a name macOS has
    since retired: republishing the old name brings the board back without a
    reflash. Empty by default — this is a migration patch, not normal config.
    """
    try:
        import config
        return list(config.read().get("legacy_hostnames") or [])
    except Exception:
        return []

# How often we recheck the local IP.
IP_INTERVAL_S = 30.0

_procs: list[subprocess.Popen] = []
_current_ip = ""
_lock = threading.Lock()
_stop = threading.Event()


def _local_ip() -> str:
    """
    The IP of the interface that actually reaches the network.

    A "connected" UDP socket sends no packet at all — it only makes the kernel
    pick a route and fill in the source address. That way we do not have to
    guess the interface name (en0 today, en7 tomorrow).
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return ""
    finally:
        s.close()


def _spawn(args: list[str]) -> bool:
    try:
        _procs.append(subprocess.Popen(
            args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        return True
    except OSError as exc:
        print(f"[announce] failed: {exc}", file=sys.stderr)
        return False


def _tear_down() -> None:
    for p in _procs:
        if p.poll() is None:
            p.terminate()
    for p in _procs:
        try:
            p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            p.kill()
    _procs.clear()


def _publish(dns_sd: str, port: int, ip: str) -> None:
    """(Re)publishes everything for the given IP. Assumes the lock is held."""
    _tear_down()

    fp = _fingerprint()

    # The service. No -P: macOS keeps the address current itself, so this one
    # does not even depend on the IP we computed.
    #
    # The instance name carries the machine so two of them read differently in
    # `dns-sd -B`; the TXT carries the id, which is what the board matches on.
    # Naming alone would not have been enough: unique names still leave the
    # board picking whichever of the two answers first.
    instance = f"{INSTANCE} ({_machine()})"
    service = [dns_sd, "-R", instance, SERVICE, "local", str(port)]
    if fp:
        service.append(f"id={fp}")
    _spawn(service)

    # Host records. `-P` is the only way through dns-sd to publish a name with
    # a chosen address; the service that comes with it is just along for the
    # ride.
    #
    # `wisp-<id>.local` is ours alone. FIXED_NAME comes after it and is shared
    # with every other install — it stays for boards that already have it in
    # NVS, and it is why the firmware prefers the service record.
    legacy = _legacy()
    mine = [f"wisp-{fp}.local"] if fp else []
    if ip:
        for i, name in enumerate(mine + [FIXED_NAME] + legacy):
            _spawn([dns_sd, "-P", f"WispHost{i}", SERVICE, "local",
                    str(port), name, ip])

    names = ", ".join([f"{instance} [id={fp or 'none'}]"] + mine
                      + [FIXED_NAME] + legacy)
    print(f"[announce] {ip or '?'}:{port} as {names}")


def _watch(dns_sd: str, port: int) -> None:
    global _current_ip
    while not _stop.wait(IP_INTERVAL_S):
        ip = _local_ip()
        if not ip or ip == _current_ip:
            continue
        with _lock:
            print(f"[announce] IP changed {_current_ip or '?'} -> {ip}, republishing")
            _current_ip = ip
            _publish(dns_sd, port, ip)


def start(port: int) -> bool:
    """Publishes the names. Returns False if it could not — never raises."""
    global _current_ip

    dns_sd = shutil.which("dns-sd")
    if not dns_sd:
        print("[announce] dns-sd not found — the board will depend on the "
              "hostname stored in NVS", file=sys.stderr)
        return False

    with _lock:
        if _procs:
            return True
        _current_ip = _local_ip()
        _publish(dns_sd, port, _current_ip)

    threading.Thread(target=_watch, args=(dns_sd, port), daemon=True).start()
    atexit.register(stop)
    return True


def stop() -> None:
    """The records live exactly as long as the dns-sd processes do."""
    _stop.set()
    with _lock:
        _tear_down()


def active() -> bool:
    return any(p.poll() is None for p in _procs)


if __name__ == "__main__":
    if not start(4666):
        raise SystemExit(1)
    print("announcing. Ctrl-C to stop.")
    try:
        while active():
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        stop()
