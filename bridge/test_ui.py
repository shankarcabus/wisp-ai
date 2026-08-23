"""
O esquema de ajustes e a sua migração.

Rode: python3 bridge/test_ui.py

SEM FRAMEWORK, como os outros testes deste diretório.

Não toca o ~/.wisp de verdade: aponta config.FOLDER, config.UI_FILE e
config.MASCOT_FILE para um diretório temporário.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import config  # noqa: E402

falhas = 0


def checa(nome, obtido, esperado):
    global falhas
    ok = obtido == esperado
    falhas += 0 if ok else 1
    print(f"  {'ok  ' if ok else 'FALHA'}  {nome}")
    if not ok:
        print(f"          esperava {esperado!r}")
        print(f"          obtive   {obtido!r}")


def limpa():
    for f in (config.UI_FILE, config.MASCOT_FILE):
        if f.exists():
            f.unlink()


def main():
    tmp = Path(tempfile.mkdtemp(prefix="wisp-ui-"))
    config.FOLDER = tmp
    config.UI_FILE = tmp / "ui.json"
    config.MASCOT_FILE = tmp / "mascot"

    PADRAO = {
        "character": "",
        "board": {"size": "medium", "action_label": True,
                  "project_label": True, "language": "en"},
        "mac": {"size": "medium",
                "sound": {"enabled": True, "states": ["asking", "waiting"]}},
    }

    print("config.ui()")
    limpa()
    checa("sem nada, devolve o padrao inteiro", config.ui(), PADRAO)

    limpa()
    config.MASCOT_FILE.write_text("bytelo\n")
    esperado = json.loads(json.dumps(PADRAO))
    esperado["character"] = "bytelo"
    checa("migracao: le o mascot antigo, resto no padrao", config.ui(), esperado)

    limpa()
    config.UI_FILE.write_text(json.dumps({
        "character": "terminal",
        "board": {"size": "large", "action_label": False,
                  "project_label": True, "language": "pt"},
        "mac": {"size": "small",
                "sound": {"enabled": False, "states": ["done"]}},
    }))
    checa("ui.json completo", config.ui(), {
        "character": "terminal",
        "board": {"size": "large", "action_label": False,
                  "project_label": True, "language": "pt"},
        "mac": {"size": "small",
                "sound": {"enabled": False, "states": ["done"]}},
    })

    limpa()
    config.UI_FILE.write_text(json.dumps({"board": {"language": "pt"}}))
    esperado = json.loads(json.dumps(PADRAO))
    esperado["board"]["language"] = "pt"
    checa("ui.json parcial: o que falta cai no padrao", config.ui(), esperado)

    limpa()
    config.UI_FILE.write_text("{ isto nao e json")
    checa("json invalido nao derruba: devolve o padrao", config.ui(), PADRAO)

    limpa()
    config.UI_FILE.write_text(json.dumps({"character": "bytelo"}))
    config.MASCOT_FILE.write_text("terminal\n")
    esperado = json.loads(json.dumps(PADRAO))
    esperado["character"] = "bytelo"
    checa("com os dois, o ui.json ganha", config.ui(), esperado)

    print()
    print("all passed" if not falhas else f"{falhas} falha(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
