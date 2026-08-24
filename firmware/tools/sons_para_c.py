#!/usr/bin/env python3
"""
Converte os sons do sistema do macOS em PCM embutido no firmware.

    python3 firmware/tools/sons_para_c.py

Escreve firmware/main/sons.c. NÃO EDITAR O .c À MÃO — rode isto.

POR QUE OS SONS DO SISTEMA
--------------------------
São os mesmos que o Mac toca (ver mac/Sources/Som.swift), então o aviso soa
igual nos dois lugares e a distinção entre chamar, errar e concluir já vem
calibrada por quem os desenhou. A alternativa era eu inventar timbres, que é
arte, e arte não se inventa por conveniência de quem escreve o firmware.

O FORMATO, E POR QUE ESTE
-------------------------
Mono, 16-bit little-endian, 22050 Hz.

Mono porque o ES8311 desta placa é um codec MONO — mandar estéreo seria pagar o
dobro de flash para o codec descartar metade. 22 kHz porque o alto-falante é de
dois pads de solda num gabinete de 2,16": o que 44,1 kHz acrescenta ali não é
audível, e custaria o dobro.

A conversão é o afconvert, que já vem no macOS. Saída WAVE e não CAF porque o
cabeçalho do WAV é previsível: o pedaço de áudio começa 8 bytes depois da marca
"data".

POR QUE EM TEMPO DE BUILD
-------------------------
Mesma regra do props_to_c.py, e o ui.c documenta a medição por trás dela:
decodificar em tempo de execução nesta placa custa o firmware inteiro. Aqui a
conta é ainda mais simples — o codec quer PCM, então decodificar na placa seria
trabalho para chegar exatamente onde este script já chega.
"""
import subprocess
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
SAIDA = RAIZ / "firmware" / "main" / "sons.c"
SISTEMA = Path("/System/Library/Sounds")

# chave em C -> arquivo do sistema. O mapa estado->som vive no fim deste
# arquivo, junto do código que o usa.
SONS = {"ping": "Ping", "glass": "Glass", "basso": "Basso"}
TAXA = 22050


def pcm(nome: str) -> bytes:
    origem = SISTEMA / f"{nome}.aiff"
    if not origem.exists():
        raise SystemExit(f"não achei {origem}")
    with tempfile.TemporaryDirectory() as tmp:
        destino = Path(tmp) / "s.wav"
        subprocess.run(["/usr/bin/afconvert",
                        "-f", "WAVE", "-d", f"LEI16@{TAXA}", "-c", "1",
                        str(origem), str(destino)],
                       check=True, capture_output=True)
        bruto = destino.read_bytes()
    i = bruto.index(b"data")
    tamanho = int.from_bytes(bruto[i + 4:i + 8], "little")
    dados = bruto[i + 8:i + 8 + tamanho]
    if len(dados) != tamanho or not dados:
        raise SystemExit(f"{nome}: chunk data diz {tamanho}, achei {len(dados)}")
    return dados


def em_c(nome: str, dados: bytes) -> str:
    linhas = [f"static const uint8_t {nome}_pcm[] = {{"]
    for i in range(0, len(dados), 16):
        linhas.append("    " + ",".join(str(b) for b in dados[i:i + 16]) + ",")
    linhas.append("};")
    return "\n".join(linhas)


RODAPE = '''
const uint8_t *som_pcm(wisp_state_t s, size_t *len)
{
    /* O mapa é o mesmo do Mac (mac/Sources/Som.swift), e fixo pela mesma razão
     * escrita lá: escolher oito sons num painel é trabalho para quem usa, e a
     * expressividade sai de graça aqui — curto e claro para quem chama, grave
     * para quem falhou, cristalino para quem terminou. */
    switch (s) {
    case WISP_DONE:  *len = sizeof(glass_pcm); return glass_pcm;
    case WISP_ERROR: *len = sizeof(basso_pcm); return basso_pcm;
    default:         *len = sizeof(ping_pcm);  return ping_pcm;
    }
}
'''


def main():
    partes = ["/* GERADO por firmware/tools/sons_para_c.py — não editar à mão.",
              " *",
              f" * PCM mono, 16-bit LE, {TAXA} Hz, dos sons do sistema do macOS. */",
              '#include "sons.h"',
              ""]
    for chave, arquivo in SONS.items():
        dados = pcm(arquivo)
        dur = len(dados) / 2 / TAXA
        print(f"  {arquivo:6} {len(dados):>7} bytes  {dur:.2f}s")
        partes.append(em_c(chave, dados))
        partes.append("")
    partes.append(RODAPE)
    SAIDA.write_text("\n".join(partes))
    print(f"escrito {SAIDA.relative_to(RAIZ)}")


if __name__ == "__main__":
    main()
