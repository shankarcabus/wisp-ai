# Os adornos do personagem pixel

Bolha de pensamento, laptop com mãos, `?`, mãos juntas, faíscas e wifi cortado.
São a parte da referência que não sai de parâmetro: dedos e um laptop não são
uma tabela de ângulos.

## O formato

Um arquivo de texto por adorno, um caractere por pixel de arte, e
`paleta.txt` dizendo qual cor é cada caractere. `.` é transparente.

Editar é editar texto: mude o mapa, rode o gerador, olhe no simulador. O ciclo
é de segundos, e é aí que o desenho acontece.

```bash
python3 firmware/tools/props_to_c.py
cmake --build sim/build -j
WISP_MASCOT=pixel ./sim/build/wisp-sim
```

## Por que os adornos flutuantes são ESCUROS

Na folha de referência a bolha, o `?`, as faíscas e o wifi são amarelos —
porque lá eles estão **fora** da cabeça, sobre o fundo preto.

Na placa não há esse fora. O mascote tem 306px numa tela de 480 e ocupa
y 37..343: sobram 37 pixels acima da cabeça, e um adorno de nove unidades de
arte a treze pixels cada mede 117. Flutuar acima sai cortado pela borda — foi
exatamente o que aconteceu na primeira tentativa.

Então eles sobrepõem o canto da cabeça, e sobre amarelo um traço amarelo não
existe. Desenhados escuros, no mesmo tom dos olhos e da boca, eles leem e ainda
pertencem à mesma cara. O laptop e as mãos continuam com amarelo, porque ali o
amarelo é a mão do personagem e o contraste vem do preto do laptop.

## Por que texto, e não PNG na partição

Três razões, em ordem de peso:

1. **A partição `storage` está apertada.** A arte do Terminal ocupa 2,2MB dos
   3MB do C6 — mais do que o comentário em `partitions.esp32c6.csv` diz, que
   fala de 1,3MB e ficou desatualizado quando a arte mudou.
2. **Um adorno é minúsculo.** O laptop tem 18×8 pixels de arte. Em RGB565A8 são
   432 bytes de cor mais 144 de alfa: não se sente no binário do app.
3. **Texto é revisável.** Um diff de mapa ASCII se lê; um diff de PNG não.

## Por que gerado em tempo de build

O `ui.c` deste projeto documenta, com medição, o que custa decodificar imagem em
tempo de execução nesta placa: o FPS caiu de 62 para 1–7 e a RAM interna chegou
a **12 bytes** de mínimo histórico. A regra do repositório é que a conversão
acontece antes de o firmware rodar. O gerador obedece.

`firmware/main/mascote_pixel_props.c` é **gerado**. Não editar à mão.
