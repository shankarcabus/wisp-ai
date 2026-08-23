"""
O canal do personagem: o que o painel publica é o que o /state carrega.

Rode: python3 bridge/test_mascot.py

SEM FRAMEWORK, como o test_limits.py deste mesmo diretório — o projeto não tem
dependência de teste e não é para ganhar uma.

Estes testes NÃO tocam o ~/.wisp de verdade: apontam config.FOLDER e
config.MASCOT_FILE para um diretório temporário. Um teste que escreve na casa de
quem roda é um teste que ninguém roda duas vezes.
"""
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
    print(f"  {'ok  ' if ok else 'FALHA'}  {nome}  — obtive {obtido!r}")


def main():
    tmp = Path(tempfile.mkdtemp(prefix="wisp-mascot-"))
    config.FOLDER = tmp
    config.MASCOT_FILE = tmp / "mascot"

    print("config.mascot()")
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
