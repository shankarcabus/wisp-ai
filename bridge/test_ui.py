"""
O esquema de ajustes, a sua migração, e o leitor do formato antigo.

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
                  "project_label": True, "language": "en",
                  "sound": {"enabled": False,
                            "states": ["asking", "waiting"],
                            "volume": "medium"}},
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
    COMPLETO = {
        "character": "terminal",
        "board": {"size": "large", "action_label": False,
                  "project_label": True, "language": "pt",
                  "sound": {"enabled": True, "states": ["error"],
                            "volume": "high"}},
        "mac": {"size": "small",
                "sound": {"enabled": False, "states": ["done"]}},
    }
    config.UI_FILE.write_text(json.dumps(COMPLETO))
    checa("ui.json completo", config.ui(), COMPLETO)

    limpa()
    config.UI_FILE.write_text(json.dumps({"board": {"language": "pt"}}))
    esperado = json.loads(json.dumps(PADRAO))
    esperado["board"]["language"] = "pt"
    checa("ui.json parcial: o que falta cai no padrao", config.ui(), esperado)

    # O ui.json escrito pela versao ANTERIOR do app nao tem board.sound. O merge
    # e recursivo, entao a secao nasce do padrao sem apagar o que o usuario ja
    # escolheu ao lado dela — e nasce MUDA, para ninguem passar a ouvir o mesmo
    # aviso duas vezes so por atualizar.
    limpa()
    config.UI_FILE.write_text(json.dumps({
        "board": {"size": "large", "language": "pt"},
        "mac": {"sound": {"enabled": False, "states": []}},
    }))
    cfg = config.ui()
    checa("ui.json antigo mantem o size", cfg["board"]["size"], "large")
    checa("ui.json antigo mantem o idioma", cfg["board"]["language"], "pt")
    checa("ui.json antigo nasce mudo na placa", cfg["board"]["sound"]["enabled"], False)
    checa("ui.json antigo ganha volume medio", cfg["board"]["sound"]["volume"], "medium")
    checa("ui.json antigo preserva o som do Mac", cfg["mac"]["sound"]["enabled"], False)
    checa("ui.json antigo preserva a lista do Mac", cfg["mac"]["sound"]["states"], [])

    limpa()
    config.UI_FILE.write_text("{ isto nao e json")
    checa("json invalido nao derruba: devolve o padrao", config.ui(), PADRAO)

    limpa()
    config.UI_FILE.write_text(json.dumps({"character": "bytelo"}))
    config.MASCOT_FILE.write_text("terminal\n")
    esperado = json.loads(json.dumps(PADRAO))
    esperado["character"] = "bytelo"
    checa("com os dois, o ui.json ganha", config.ui(), esperado)

    # ── o leitor do formato antigo ──
    #
    # Ele existe só como migração — ui() cai nele quando não há ui.json — e por
    # isso mora aqui e não num arquivo próprio: um segundo arquivo de teste
    # recriava este mesmo arcabouço inteiro (sys.path, `falhas`, `checa`, o
    # epílogo) para exercitar quatro linhas, e as duas cópias já tinham
    # divergido no formato da saída.
    print()
    print("config.mascot()")
    limpa()
    checa("sem arquivo, devolve vazio", config.mascot(), "")

    config.MASCOT_FILE.write_text("bytelo\n")
    checa("le o nome publicado", config.mascot(), "bytelo")

    config.MASCOT_FILE.write_text("  terminal  \n\n")
    checa("tira espaco das duas pontas", config.mascot(), "terminal")

    config.MASCOT_FILE.write_text("bytelo")
    checa("aceita sem a quebra de linha", config.mascot(), "bytelo")

    config.MASCOT_FILE.write_text("\n")
    checa("arquivo so com quebra e vazio", config.mascot(), "")

    print()
    print("all passed" if not falhas else f"{falhas} falha(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
