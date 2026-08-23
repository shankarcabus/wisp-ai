"""
Per-user configuration. Nothing machine-specific in the source.

Lives in ~/.wisp/config.json, created on first use with sensible values.
The file is 0600 because it holds the bridge access token.

WHY THIS EXISTS
---------------
Until now one person's Mac hostname was hardcoded in announce.py and São Paulo
was hardcoded in weather.py. That works on one machine; it is garbage on every
other one. Publishing like that would mean every user editing source code to
change their city.

And the more serious part: the bridge listens on the local network and serves
project names and usage to whoever asks. On a coworking network that is a free
read. The token here fixes it — the board sends it, nobody else has it.
"""

from __future__ import annotations

import json
import os
import secrets
import urllib.request
from pathlib import Path

FOLDER = Path.home() / ".wisp"
FILE = FOLDER / "config.json"

# A escolha de personagem publicada pelo painel do Mac.
#
# ARQUIVO PRÓPRIO, E NÃO UMA CHAVE NO config.json
# -----------------------------------------------
# Quem escreve é o app, em Swift. Para publicar um escalar no config.json ele
# teria de reimplementar o merge de defaults, a geração de token e o 0600 deste
# módulo — muito código para pouca coisa — e dois escritores no mesmo arquivo
# convidam a corrida. Uma linha, gravada por temp+rename do lado do app, e a
# leitura mora aqui porque a pasta é deste módulo.
MASCOT_FILE = FOLDER / "mascot"

# Os ajustes de interface publicados pelo painel do Mac.
#
# Substitui o MASCOT_FILE de uma linha, que ficou pequeno na primeira vez que se
# quis ajustar mais de uma coisa. O antigo continua sendo LIDO, como migração:
# ver ui() abaixo.
UI_FILE = FOLDER / "ui.json"

# Os padrões, e o contrato do formato num lugar só.
#
# Chaves e valores em inglês, como as do config.json — o que é em português é o
# texto que aparece na tela, não a configuração. Um arquivo em que ninguém sabe
# qual idioma esperar é um arquivo que se erra ao editar.
UI_DEFAULTS = {
    "character": "",
    "board": {"size": "medium", "action_label": True,
              "project_label": True, "language": "en"},
    "mac": {"size": "medium"},
}

DEFAULTS = {
    "port": 4666,

    # Token the board presents on every request. Generated on first use.
    "token": "",

    # With the token required, /state only answers whoever presents the secret.
    # A fresh install is born true. It only goes false while there is a board
    # running old firmware that does not know how to send the token yet.
    "require_token": True,

    # Weather forecast. Without this the bridge tries to figure it out from
    # your IP on first use; failing that, the clock shows up with no
    # temperature.
    "weather": {"lat": None, "lon": None, "name": ""},

    # Old hostnames to republish over mDNS, so already-flashed boards keep
    # finding the bridge after macOS renames the machine.
    "legacy_hostnames": [],
}


def _detect_location() -> dict:
    """
    Finds the city from the public IP, once, on first use.

    It is the difference between "it just works" and "edit a JSON before using
    it". One call, saved forever. If it fails the weather simply does not show
    up — it is never a reason to bring anything down.
    """
    try:
        req = urllib.request.Request(
            "http://ip-api.com/json/?fields=status,city,lat,lon",
            headers={"User-Agent": "wisp"})
        with urllib.request.urlopen(req, timeout=6) as r:
            d = json.loads(r.read())
        if d.get("status") == "success":
            return {"lat": d["lat"], "lon": d["lon"], "name": d.get("city", "")}
    except Exception:
        pass
    return {"lat": None, "lon": None, "name": ""}


def _merge(base: dict, new: dict) -> dict:
    """Keeps the keys the user already has and adds the missing ones."""
    out = dict(base)
    for k, v in (new or {}).items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _merge(out[k], v)
        else:
            out[k] = v
    return out


_cache: dict | None = None


def read(reload: bool = False) -> dict:
    global _cache
    if _cache is not None and not reload:
        return _cache

    current = {}
    if FILE.exists():
        try:
            current = json.loads(FILE.read_text())
        except (OSError, json.JSONDecodeError):
            current = {}

    cfg = _merge(DEFAULTS, current)
    changed = cfg != current

    if not cfg["token"]:
        # 32 urlsafe bytes: plenty for a local network and small enough for a
        # header.
        cfg["token"] = secrets.token_urlsafe(24)
        changed = True

    w = cfg["weather"]
    if w.get("lat") is None and "weather_detected" not in current:
        cfg["weather"] = _detect_location()
        # Record the attempt even if it failed: we do not retry on every boot.
        cfg["weather_detected"] = True
        changed = True

    if changed:
        write(cfg)

    _cache = cfg
    return cfg


def write(cfg: dict) -> None:
    FOLDER.mkdir(mode=0o700, parents=True, exist_ok=True)
    tmp = FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(cfg, indent=2, ensure_ascii=False))
    os.chmod(tmp, 0o600)   # it holds the token
    tmp.replace(FILE)


def mascot() -> str:
    """
    O personagem que o painel publicou, ou "" quando ninguém escolheu.

    Vazio é um resultado legítimo e frequente: instalação nova, ou alguém que
    nunca abriu o seletor. Quem consome trata ausência como "não mande o campo",
    e a placa trata campo ausente como "mantenha o que tem" — nem instalação
    nova nem bridge velho devem derrubar a escolha que já está na NVS.
    """
    try:
        return MASCOT_FILE.read_text().strip()
    except OSError:
        return ""


def ui() -> dict:
    """
    Os ajustes, SEMPRE com todas as chaves preenchidas.

    Quem consome não precisa perguntar se a chave existe, e é por isso que o
    merge é aqui: espalhar `.get(chave, padrao)` por três arquivos é como se cria
    três padrões diferentes para a mesma coisa.

    A MIGRAÇÃO NÃO TEM PASSO
    ------------------------
    Sem o ui.json, tenta o `mascot` de uma linha e usa só o personagem dele. O
    app escreve o ui.json na primeira mudança que alguém fizer; ninguém roda nada
    e nada se perde. Com os dois presentes, o ui.json ganha — ele é o novo.

    JSON inválido devolve o padrão. Um arquivo que alguém editou à mão e errou uma
    vírgula não pode derrubar o bridge: o custo do erro é a configuração ignorada,
    e isso aparece na tela.
    """
    cfg = _merge(UI_DEFAULTS, {})
    try:
        cfg = _merge(cfg, json.loads(UI_FILE.read_text()))
    except (OSError, json.JSONDecodeError):
        antigo = mascot()
        if antigo:
            cfg["character"] = antigo
    return cfg


if __name__ == "__main__":
    c = read()
    print(f"config: {FILE}")
    print(f"  port          : {c['port']}")
    print(f"  token         : {c['token'][:6]}… ({len(c['token'])} chars)")
    print(f"  require_token : {c['require_token']}")
    w = c["weather"]
    print(f"  weather       : {w['name'] or '—'} ({w['lat']}, {w['lon']})")
    print(f"  legacy        : {c['legacy_hostnames'] or '—'}")
