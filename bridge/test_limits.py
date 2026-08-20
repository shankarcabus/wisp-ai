"""
Regression tests for how the limits are chosen. Run it: python3 test_limits.py

WHY THIS FILE EXISTS
--------------------
It locks down a bug that put "35608 min ago" on the board — 24.7 days, with
"88% weekly" from a week that had closed in July, while the real week sat at
41%. Two defects met to produce it:

  1. A payload whose windows had all reset was served as if it still meant
     something. It does not: after a reset the real usage DROPS, so the old
     percentage does not just go stale, it goes wrong in the direction that
     alarms you.

  2. The live reading was given a 10-minute deadline, after which the on-disk
     cache won no matter how old it was. But the app only refetches hourly when
     nobody is working, so for most of every hour the freshest number on the
     machine was thrown away in favour of a file Claude Code had not touched in
     three weeks.

Both are silent failures — nothing raises, nothing logs, the screen just shows
a wrong number with an honest-looking age next to it. That is exactly the kind
of bug a test has to hold down, because nobody will notice it coming back.

NO FRAMEWORK, ON PURPOSE
------------------------
Same reason the bridge itself has no dependencies: it runs on the system
python3, which never changes. A test suite that needs installing is a test
suite that does not get run here.
"""

from __future__ import annotations

import threading
import time
from datetime import datetime, timedelta, timezone

import limits
import server

FAILURES = []


def check(name: str, ok: bool, detail: str = "") -> None:
    print(("  ok    " if ok else "  FAIL  ") + name + (f"  — {detail}" if detail else ""))
    if not ok:
        FAILURES.append(name)


def _iso(**delta) -> str:
    return (datetime.now(timezone.utc) + timedelta(**delta)).isoformat()


def _bar(kind: str, pct: int, resets_at: str, active: bool = False) -> dict:
    return {"kind": kind, "percent": pct, "severity": "normal",
            "resets_at": resets_at, "is_active": active}


def _state(live, live_age_s: int) -> server.State:
    """A State with only what subscription_limits() touches — no server, no threads."""
    st = server.State.__new__(server.State)
    st.lock = threading.Lock()
    st.live_limits = live
    st.live_limits_at = time.time() - live_age_s
    st.limits_error = ""
    return st


CLOSED = {"limits": [
    _bar("session", 3, _iso(days=-24)),
    _bar("weekly_all", 88, _iso(days=-24), active=True),
]}
OPEN = {"limits": [
    _bar("session", 2, _iso(hours=3), active=True),
    _bar("weekly_all", 41, _iso(days=5)),
]}
# The ordinary case: the 5h window resets while the weekly one is still open.
# This one must NOT be refused — half the payload is still true.
HALF = {"limits": [
    _bar("session", 3, _iso(minutes=-2)),
    _bar("weekly_all", 41, _iso(days=5), active=True),
]}


print("\nnormalize()")

r = limits.normalize(CLOSED, age_s=24 * 86400)
check("refuses a payload whose every window has reset",
      not r.get("ok"), f"got ok={r.get('ok')} peak={r.get('peak')}")

r = limits.normalize(HALF, age_s=600)
check("keeps a payload where only some windows reset",
      r.get("ok") and [b["expired"] for b in r["bars"]] == [True, False],
      f"got ok={r.get('ok')} expired={[b.get('expired') for b in r.get('bars', [])]}")

r = limits.normalize(OPEN, age_s=1200)
check("passes a payload with every window open",
      r.get("ok") and r.get("peak") == 41, f"got {r}")


print("\nsubscription_limits() — the criterion is age, not provenance")

_real_read = limits.read
try:
    # 20-minute live reading against the 24-day cache that caused the bug. The
    # old code took the cache here, because 20 min was past its deadline.
    limits.read = lambda: limits.normalize(CLOSED, age_s=24 * 86400)
    r = _state(OPEN, live_age_s=1200).subscription_limits()
    check("live beats a cache from three weeks ago",
          r.get("source") == "live" and r.get("age_s") == 1200,
          f"chose {r.get('source')!r} with age_s={r.get('age_s')}")

    # The other direction, which is what the old deadline was there to protect:
    # if the app stops fetching, the cache has to take over. Comparing ages
    # does that on its own, with no deadline to tune.
    limits.read = lambda: limits.normalize(OPEN, age_s=60)
    r = _state(OPEN, live_age_s=7200).subscription_limits()
    check("a fresh cache beats a stale live reading",
          r.get("source") == "cache" and r.get("age_s") == 60,
          f"chose {r.get('source')!r} with age_s={r.get('age_s')}")

    # Neither source usable: the caller needs the REASON, because that is what
    # tells a human whether to run /usage or to go look at the keychain.
    limits.read = lambda: {"ok": False, "reason": "no cachedUsageUtilization (run /usage once)"}
    r = _state(None, live_age_s=0).subscription_limits()
    check("with no usable source, the reason survives",
          not r.get("ok") and "usage" in r.get("reason", ""), f"got {r}")

    # The whole point, stated as the invariant: never serve an age longer than
    # the window the number describes. A weekly bar cannot be 24 days old.
    limits.read = lambda: limits.normalize(CLOSED, age_s=24 * 86400)
    r = _state(OPEN, live_age_s=1200).subscription_limits()
    check("never serves an age longer than the window it describes",
          r.get("age_s", 0) <= 7 * 86400,
          f"age_s={r.get('age_s')} ({r.get('age_s', 0) // 60} min)")
finally:
    limits.read = _real_read


print()
if FAILURES:
    print(f"FAILED: {len(FAILURES)} — {', '.join(FAILURES)}")
    raise SystemExit(1)
print("all passed")
