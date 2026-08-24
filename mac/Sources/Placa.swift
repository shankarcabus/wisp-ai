import SwiftUI

/// A tela da placa, no Mac.
///
/// POR QUE UMA RÉPLICA, E NÃO UMA TRADUÇÃO
/// ---------------------------------------
/// O painel tinha o mesmo dado da placa desenhado de outra maneira: mascote
/// pequeno num cabeçalho, limites como quatro linhas finas. Duas gramáticas
/// visuais para a mesma informação, e a conta de aprender as duas é de quem usa.
/// Aqui o Mac mostra a tela da placa — a mesma composição, as mesmas cores, os
/// mesmos limiares — e quem olha para os dois vê uma coisa só.
///
/// OS NÚMEROS SÃO OS DO FIRMWARE, LITERALMENTE
/// -------------------------------------------
/// Todo tamanho e toda posição aqui estão escritos em PIXELS DA PLACA, os 480
/// de `firmware/main/ui.c`, e convertidos por `Vidro.esc` na hora de desenhar.
/// É deliberado, e é o que torna a fidelidade verificável: cada constante deste
/// arquivo se procura no `ui.c` e se confere. Reescrevê-las em pontos do Mac
/// pareceria mais limpo e cortaria justamente esse elo — na primeira mudança do
/// firmware ninguém saberia mais qual número daqui correspondia a qual de lá.
///
/// AS TRÊS DIVERGÊNCIAS DELIBERADAS
/// --------------------------------
/// Estão marcadas uma por uma no ponto onde acontecem, com o motivo. Em resumo:
/// janela expirada, a procedência do número nos limites, e a fonte — que é a
/// única involuntária, porque a Montserrat do LVGL não vem no macOS.

// ————————————————————————————————————————————————
//  O vidro: 480x480, e a régua para caber no painel
// ————————————————————————————————————————————————

/// O sistema de coordenadas da placa.
///
/// `p(_:)` converte pixel da placa em ponto do Mac e `fonte(_:)` faz o mesmo com
/// os tamanhos das Montserrat embutidas no LVGL, que também são pixels.
///
/// A FONTE É A DIVERGÊNCIA QUE NÃO DÁ PARA FECHAR. A placa usa Montserrat, que
/// não vem no macOS; um `.custom("Montserrat", …)` cairia silenciosamente na
/// fonte do sistema em qualquer máquina que não a tenha instalada, e "às vezes
/// idêntico" é pior do que "sempre próximo". A SF no mesmo corpo em pixels dá a
/// mesma massa de texto no mesmo lugar, que é o que a composição precisa.
struct Vidro {
    /// O lado do quadrado, em pontos do Mac.
    let lado: CGFloat

    /// Os 480px do painel AMOLED, iguais nas duas placas — o comentário de
    /// `vaga_de()` registra que a resolução não depende do MCU.
    static let LADO_PLACA: CGFloat = 480

    var esc: CGFloat { lado / Vidro.LADO_PLACA }

    func p(_ px: CGFloat) -> CGFloat { px * esc }

    func fonte(_ px: CGFloat, _ peso: Font.Weight = .regular) -> Font {
        .system(size: px * esc, weight: peso)
    }

    /// Um ponto dado em coordenada da placa, com o zero no CENTRO — que é como
    /// o `LV_ALIGN_CENTER` do LVGL conta, e como quase todo `lv_obj_align` do
    /// `ui.c` está escrito.
    func centro(_ dx: CGFloat, _ dy: CGFloat) -> CGPoint {
        CGPoint(x: lado / 2 + p(dx), y: lado / 2 + p(dy))
    }
}

/// As cores da placa, uma a uma, do arquivo onde estão escritas.
///
/// Não há aqui nenhuma cor que o firmware não tenha. Onde o painel do Mac
/// precisava de um tom que a placa não usa, ele usa `Palette`, que é a paleta do
/// Mac e vive em `Panel.swift` — as duas não se misturam, e é por isso que este
/// enum não tem nenhum `Color.secondary`.
enum Cores {
    private static func c(_ r: Int, _ g: Int, _ b: Int) -> Color {
        Color(red: Double(r) / 255, green: Double(g) / 255, blue: Double(b) / 255)
    }

    // ui_create(): o fundo da tela ativa é preto puro, e é o que o AMOLED
    // desliga em vez de pintar.
    static let fundo = Color.black

    // criar_rotulos()
    static let detalhe  = c(230, 233, 238)
    static let projetos = c(134, 144, 158)

    // criar_painel()
    static let titulo   = c(150, 158, 172)
    static let cartao   = c( 24,  24,  30)
    static let pct      = c(238, 242, 248)
    static let trilha   = c( 62,  56,  88)
    static let veu      = c(238, 242, 248)   // sobre a barra, a 20%
    static let reset    = c(140, 150, 164)
    static let frescor  = c(120, 128, 140)
    static let frescorVelho = c(232, 193,  90)

    // ui_update(): a pílula acesa é o limite que está valendo.
    static let pilulaAcesaFundo  = c( 96,  86, 146)
    static let pilulaAcesaTexto  = c(236, 238, 246)
    static let pilulaApagadaFundo = c( 52,  50,  66)
    static let pilulaApagadaTexto = c(150, 156, 170)

    // modo repouso
    static let hora   = c(238, 242, 248)
    static let dia    = c(140, 150, 164)
    static let temp   = c(232, 132,  90)
    static let cond   = c(184, 192, 204)
    static let maxmin = c(122, 130, 142)

    // bateria: cinza como a data, âmbar como a temperatura abaixo de 20%.
    static let bateria      = c(140, 150, 164)
    static let bateriaBaixa = c(232, 132,  90)

    // Ícones de tempo — C_SOL, C_LUA, C_NUVEM, C_CHUVA no topo do ui.c.
    static let sol   = c(240, 176,  72)
    static let lua   = c(226, 232, 242)
    static let nuvem = c(150, 160, 176)
    static let chuva = c(104, 162, 214)
    static let raio  = c(240, 200,  80)

    /// Cor pela FAIXA de uso — `cor_do_pct()`, verbatim.
    ///
    /// Cinco faixas de 20 pontos, de frio para quente. A leitura pretendida é
    /// periférica: dá para saber onde se está sem ler o número. É esta função,
    /// e não o campo `severity` do bridge, que decide a cor da barra — o que o
    /// `severity` responderia é outra pergunta, e quem a responde é a pílula.
    static func doPct(_ pct: Int) -> Color {
        if pct > 80 { return c(226,  74,  62) }   // vermelho        81-100
        if pct > 60 { return c(240, 146,  58) }   // laranja          61-80
        if pct > 40 { return c(238, 206,  70) }   // amarelo          41-60
        if pct > 20 { return c(166, 214,  86) }   // verde-amarelado  21-40
        return          c( 76, 200, 176)          // verde-azulado     0-20
    }
}

/// Os oito estados como a PLACA os escreve.
///
/// Não é `MascotState.label`: as duas tabelas discordam em três estados —
/// `asking`, `done` e `offline` — e a réplica tem de dizer o que a placa diz.
/// Juntá-las numa só seria a correção errada: o painel do Mac tem espaço para
/// "no connection" e a placa não, e é por isso que lá está "offline".
///
/// `NOME` e `NOME_PT` no `ui.c`. Nenhuma das oito em português leva acento, e
/// isso é escolha e não sorte — as Montserrat do LVGL não têm acento.
enum NomeNaPlaca {
    static func en(_ s: MascotState) -> String {
        switch s {
        case .idle:    return "idle"
        case .working: return "thinking"
        case .tool:    return "working"
        case .asking:  return "asking you"
        case .waiting: return "needs you"
        case .done:    return "done"
        case .error:   return "failed"
        case .offline: return "offline"
        }
    }

    static func pt(_ s: MascotState) -> String {
        switch s {
        case .idle:    return "parado"
        case .working: return "pensando"
        case .tool:    return "trabalhando"
        case .asking:  return "perguntando"
        case .waiting: return "pedindo ajuda"
        case .done:    return "pronto"
        case .error:   return "falhou"
        case .offline: return "desligado"
        }
    }
}

// ————————————————————————————————————————————————
//  A moldura
// ————————————————————————————————————————————————

/// Um quadrado preto com o canto arredondado, onde uma tela da placa mora.
///
/// A borda de 8% não está no firmware, e é o único acréscimo puramente visual
/// deste arquivo: sem ela o preto puro do AMOLED encosta no fundo do popover,
/// que também é escuro, e a réplica lê como um buraco no painel em vez de uma
/// tela. O raio imita o vidro, cuja beirada arredondada o `vaga_de()` já
/// menciona ao explicar os 37px de folga no topo.
struct Tela<Conteudo: View>: View {
    let lado: CGFloat
    /// Sem parâmetro: cada tela deriva o próprio `Vidro` do GeometryReader que
    /// já precisa ter. Passar um daqui para dentro daria duas réguas para a
    /// mesma medida — e é assim que uma delas fica para trás.
    @ViewBuilder var conteudo: Conteudo

    init(lado: CGFloat, @ViewBuilder conteudo: () -> Conteudo) {
        self.lado = lado
        self.conteudo = conteudo()
    }

    var body: some View {
        let v = Vidro(lado: lado)
        ZStack { conteudo }
            .frame(width: lado, height: lado)
            .background(Cores.fundo)
            .clipShape(RoundedRectangle(cornerRadius: v.p(46), style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: v.p(46), style: .continuous)
                    .strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
            )
    }
}

// ————————————————————————————————————————————————
//  Tile 0, modo mascote
// ————————————————————————————————————————————————

/// O mascote e seus dois rótulos, na geometria de UMA sessão.
///
/// SÓ O ARRANJO DE UMA SESSÃO, E ISSO É UMA ESCOLHA
/// ------------------------------------------------
/// A placa divide a tela entre até quatro mascotes (`vaga_de()`), e o
/// `ui_update()` do firmware fixa uma sessão de propósito — a mais urgente, com
/// a marca `> ` na lista dizendo qual. Então o arranjo de um é o que a placa
/// realmente mostra, e replicar as grades de 2 e 4 seria replicar um caminho que
/// o firmware não toma.
///
/// A conta vertical, do `vaga_de()`: mascote de 306px centrado em y=190 ocupa
/// 37..343, o detalhe cai em 369, a lista começa em 395, e quatro linhas de 19
/// fecham em 471.
struct TelaMascote: View {
    @ObservedObject var bridge: Bridge

    /// Os degraus de tamanho, das duas tabelas do firmware.
    ///
    /// São DUAS porque na placa a palavra "small" não quer dizer o mesmo para
    /// cada personagem — é o que o comentário de `wisp_tam_t` diz, e as duas
    /// tabelas provam. O Bytelo recebe menos da vaga porque a margem que sobra é
    /// onde os adornos dele moram, e é ela que limita o degrau grande a 80%.
    ///
    /// CHAVEADAS PELO NOME, e não pelo caminho de desenho.
    ///
    /// A primeira versão perguntava "tem PNG?" e usava a tabela do Terminal se
    /// sim, a do Bytelo se não. Soava mais geral e estava errada no caso que roda
    /// nesta máquina: o Bytelo TEM arte em `~/.wisp/mascots/bytelo/`, então o Mac
    /// o desenhava a 70% enquanto a placa o desenha a 60%. Quem decide na placa é
    /// o personagem escolhido — `mascote_por_nome()` —, não como ele é rasterizado.
    ///
    /// Personagem novo na placa é um `.c` novo lá e uma linha nova aqui. É o
    /// mesmo acoplamento que o resto deste arquivo assume, e o `default` garante
    /// que o desconhecido apareça em vez de sumir — como o `MASCOTES[0]` faz lá.
    private static let DEGRAUS: [String: [Double]] = [
        "terminal": [0.70, 1.00, 1.18],   // PCT      em mascote_terminal.c
        "bytelo":   [0.60, 0.72, 0.80],   // CARA_PCT em mascote_bytelo.c
    ]
    private static let DEGRAU_PADRAO = [0.70, 1.00, 1.18]   // MASCOTES[0]

    private var estado: MascotState {
        guard bridge.state.alive else { return .offline }
        return bridge.data?.dominantState ?? .idle
    }

    private var sessoes: [Session] { bridge.data?.sessions ?? [] }

    /// O texto do rótulo de ação, com a regra do idioma do firmware.
    ///
    /// EM PORTUGUÊS O ESTADO GANHA DO DETALHE. Em inglês vale a regra de sempre
    /// — o detalhe quando existe, o nome do estado quando não —, porque o
    /// detalhe diz QUAL ferramenta está rodando e é mais informativo. Em
    /// português não dá: o detalhe vem do bridge e não é traduzível, são nomes
    /// próprios ("Bash", "Read") e frases geradas em inglês ("approve plan"). Um
    /// rótulo dizendo "Bash" com a tela em português está pela metade.
    private var detalhe: String {
        let pt = Ajustes.boardLanguage == "pt"
        if pt { return NomeNaPlaca.pt(estado) }
        let d = sessoes.first { MascotState($0.st) == estado }?.dt ?? ""
        return d.isEmpty ? NomeNaPlaca.en(estado) : d
    }

    /// A lista de projetos, uma linha por sessão, com `> ` na que o mascote
    /// está representando. ASCII puro no marcador, como na placa: as Montserrat
    /// embutidas cobrem só ASCII e um "›" bonito sairia como quadrado vazio.
    private var lista: [String] {
        let escolhida = sessoes.firstIndex { MascotState($0.st) == estado } ?? 0
        let mostrar = Array(sessoes.prefix(4))
        var linhas = mostrar.enumerated().map { i, s in
            (i == escolhida ? "> " : "  ") + String(s.pj.prefix(24))
        }
        // Mais sessões do que caberia: o resto volta a ser contagem. Silenciar
        // as excedentes seria mentir sobre o que está rodando.
        if sessoes.count > mostrar.count {
            linhas.append("  +\(sessoes.count - mostrar.count)")
        }
        return linhas
    }

    var body: some View {
        GeometryReader { g in
            let v = Vidro(lado: g.size.width)
            let degrau = Ajustes.Tamanho.allCases
                .firstIndex(of: Ajustes.boardSize) ?? 1
            let pct = (Self.DEGRAUS[Sprites.chosen] ?? Self.DEGRAU_PADRAO)[degrau]
            // 306 é a vaga, e o degrau é o que o personagem faz com ela.
            let vaga: CGFloat = 306
            let corpo = v.p(vaga * pct)

            let verAcao = Ajustes.boardAction
            let verProj = Ajustes.boardProject
            // Sem rótulos, o deslocamento que abria espaço para eles perde a
            // razão de ser: com uma sessão o mascote volta ao centro da tela.
            let cy: CGFloat = (!verAcao && !verProj) ? 0 : -50

            // topLeading, e não o `.center` que um ZStack dá de graça.
            //
            // Os dois jeitos de posicionar aqui NÃO são intercambiáveis, e
            // confundi-los é o defeito que a primeira versão deste arquivo
            // tinha: `.position` fixa o CENTRO do que recebe, e o
            // `LV_ALIGN_TOP_MID` do LVGL fixa o TOPO. Posicionando a lista de
            // projetos pelo centro, ela subia meia altura e entrava por cima do
            // rótulo de ação — e com três sessões o nome do primeiro projeto
            // desaparecia debaixo dele.
            //
            // Então: `.position` para o que o firmware alinha pelo centro, e
            // `.offset` num ZStack topLeading para o que ele alinha pelo topo.
            ZStack(alignment: .topLeading) {
                // O mascote é o FUNDO — dito, e não herdado da ordem de
                // criação, que foi como a placa aprendeu a errar isso.
                //
                // O `Mascot` desenha dentro de `side * 1.42`, então a `side`
                // que produz um corpo de `corpo` pontos é `corpo / 1.42`.
                Mascot(state: estado, side: corpo / 1.42)
                    .position(v.centro(0, cy))

                if verAcao {
                    Text(detalhe)
                        .font(v.fonte(32, .medium))
                        .foregroundStyle(Cores.detalhe)
                        .lineLimit(1)
                        .position(v.centro(0, cy + vaga / 2 + 26))
                }

                if verProj && !lista.isEmpty {
                    // FONTE PELA QUANTIDADE DE LINHAS, e a conta é o motivo: no
                    // montserrat_24 uma linha mede 28px e cabem duas nos 75px
                    // que sobram; de três em diante são 19px no montserrat_16.
                    // A alternativa seria cortar a lista, e aí a fonte grande
                    // custaria a informação.
                    let poucas = lista.count <= 2
                    // line_space -1 comprime as linhas de 20 para 19px, e é o
                    // que garante a quarta dentro da tela.
                    VStack(alignment: .center, spacing: v.p(poucas ? 4 : -1)) {
                        ForEach(lista, id: \.self) { linha in
                            Text(linha)
                                .font(v.fonte(poucas ? 24 : 16))
                                .foregroundStyle(Cores.projetos)
                                .lineLimit(1)
                        }
                    }
                    .frame(width: g.size.width, alignment: .center)
                    .offset(y: v.p(240 + cy + vaga / 2 + 52))
                }

                bateria(v, g.size.width)
            }
        }
    }

    /// A bateria fica FORA da lista de objetos do repouso de propósito: é o
    /// único elemento que vale nos dois modos. Mascote na tela ou relógio na
    /// tela, a pergunta "dá para ficar sem cabo?" continua valendo.
    ///
    /// Sem medida o rótulo fica VAZIO em vez de "--%": placa no cabo, sem célula
    /// instalada, é uma configuração legítima, e anunciar ignorância ali seria
    /// ruído permanente para quem nunca vai usar bateria.
    @ViewBuilder
    private func bateria(_ v: Vidro, _ largura: CGFloat) -> some View {
        if let d = bridge.data, let pct = d.batteryPct {
            Text("\(pct)%" + (d.batteryCharging ? " ⚡" : ""))
                .font(v.fonte(20))
                .foregroundStyle(d.batteryLow ? Cores.bateriaBaixa : Cores.bateria)
                // TOP_RIGHT com recuo (40, 26), e por isso um frame com
                // alinhamento em vez de `.position`: o que o firmware fixa aqui
                // é a BORDA DIREITA do rótulo, e centrar em `largura - 40`
                // jogaria metade do texto para dentro dos 40px de folga. Essa
                // folga é deliberada — a moldura come a beirada do vidro, mais
                // ainda nos cantos arredondados.
                .frame(width: largura - v.p(40), alignment: .trailing)
                .offset(y: v.p(26))
        }
    }
}

// ————————————————————————————————————————————————
//  Tile 0, modo repouso
// ————————————————————————————————————————————————

/// Relógio e tempo, quando tudo está ocioso há mais de cinco minutos.
///
/// É um MODO, e não um canto da tela do mascote. Os seis objetos —
/// `g_hora`, `g_dia`, `g_icone`, `g_temp`, `g_cond`, `g_maxmin` — vivem numa
/// lista que aparece e desaparece junta no `ui.c`, e enquanto ela está na tela o
/// mascote está escondido. Só a bateria fica nos dois.
///
/// O `REST_S` do bridge é 300s: cinco minutos, e não dois, porque em dois o
/// relógio entrava em pausas de trabalho comuns.
struct TelaRepouso: View {
    @ObservedObject var bridge: Bridge

    /// A hora e o dia o Mac formata sozinho.
    ///
    /// O bridge manda `clk` e `day` pré-formatados PARA A PLACA, e o motivo é a
    /// placa: um ESP32 não faz conta de calendário de graça. O Mac faz, e o
    /// `/app` por isso nunca carregou esses dois campos — pedir que passasse a
    /// carregá-los seria mandar bytes de ida e volta para saber que horas são
    /// aqui. O formato é o mesmo: `%H:%M` e `%a %d %b`, em C locale, que é o que
    /// a placa recebe.
    private static let hora: DateFormatter = {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "HH:mm"
        return f
    }()

    private static let dia: DateFormatter = {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "E dd MMM"
        return f
    }()

    var body: some View {
        GeometryReader { g in
            let v = Vidro(lado: g.size.width)
            // Um tique por minuto seria o certo, mas TimelineView não tem
            // "a cada minuto no minuto": .periodic de 60s desliza. O relógio
            // fica no segundo redondo pedindo o minuto seguinte.
            TimelineView(.periodic(from: .now, by: 1)) { ctx in
                let agora = ctx.date
                ZStack {
                    Text(Self.hora.string(from: agora))
                        .font(v.fonte(48, .medium))
                        .foregroundStyle(Cores.hora)
                        .position(v.centro(0, -156))

                    Text(Self.dia.string(from: agora))
                        .font(v.fonte(28))
                        .foregroundStyle(Cores.dia)
                        .position(v.centro(0, -108))

                    if let w = bridge.data?.weather {
                        IconeTempo(nome: w.i)
                            .frame(width: v.p(150), height: v.p(150))
                            .position(v.centro(0, -6))

                        // A temperatura passa dos 48px da maior Montserrat
                        // embutida por um transform de 333/256. Na placa isso é
                        // barato porque a temperatura muda a cada 15 min; aqui
                        // é só um corpo de fonte.
                        Text("\(w.t)°C")
                            .font(v.fonte(48 * 333 / 256, .semibold))
                            .foregroundStyle(Cores.temp)
                            .position(v.centro(0, 100))

                        Text(w.c)
                            .font(v.fonte(28))
                            .foregroundStyle(Cores.cond)
                            .position(v.centro(0, 148))

                        Text("max \(w.hi)°   min \(w.lo)°")
                            .font(v.fonte(24))
                            .foregroundStyle(Cores.maxmin)
                            .position(v.centro(0, 188))
                    }
                }
            }
        }
    }
}

/// Os oito ícones de tempo, das mesmas primitivas que a placa usa.
///
/// Na placa cada ícone é montado de discos e pílulas porque desenhar assim é o
/// que um ESP32 faz sem custo — e o conjunto é pequeno de propósito: variedade
/// custa código quando cada ícone é geometria escrita à mão. Aqui as duas
/// primitivas são `Circle` e `Capsule`, e o `montar_icone()` se lê de cima a
/// baixo neste corpo.
///
/// As coordenadas são da caixa de 150x150 do `g_icone`, com o zero no centro.
struct IconeTempo: View {
    /// `sun`, `moon`, `cloudsun`, `cloudmoon`, `cloud`, `rain`, `snow`, `storm`
    /// — o conjunto que `weather.py:_icon()` produz e o firmware sabe desenhar.
    let nome: String

    private static let CAIXA: CGFloat = 150

    var body: some View {
        GeometryReader { g in
            let esc = g.size.width / Self.CAIXA
            ZStack {
                switch nome {
                case "sun", "moon":
                    astro(esc, lua: nome == "moon")
                case "cloudsun", "cloudmoon":
                    if nome == "cloudmoon" {
                        // A lua recortada: disco claro com um disco PRETO por
                        // cima. Preto e não transparente porque na placa o fundo
                        // é preto puro, e o recorte é feito de tinta.
                        disco(40, Cores.lua, 22, -22, esc)
                        disco(34, Cores.fundo, 34, -30, esc)
                    } else {
                        disco(38, Cores.sol, 20, -22, esc)
                        pilula(8, 20, Cores.sol, 20, -50, esc)
                        pilula(20, 8, Cores.sol, 48, -22, esc)
                    }
                    nuvem(-6, 14, esc)
                case "cloud":
                    nuvem(0, 0, esc)
                default:
                    nuvem(0, -12, esc)
                    precipitacao(esc)
                }
            }
            .frame(width: g.size.width, height: g.size.width)
        }
        .aspectRatio(1, contentMode: .fit)
    }

    /// Sol de oito raios, ou lua recortada. `!strcmp(nome, "sun")` no ui.c.
    @ViewBuilder
    private func astro(_ esc: CGFloat, lua: Bool) -> some View {
        if lua {
            disco(62, Cores.lua, 0, 0, esc)
            disco(54, Cores.fundo, 18, -8, esc)
        } else {
            disco(52, Cores.sol, 0, 0, esc)
            pilula(10, 34, Cores.sol,   0, -44, esc)
            pilula(10, 34, Cores.sol,   0,  44, esc)
            pilula(34, 10, Cores.sol, -44,   0, esc)
            pilula(34, 10, Cores.sol,  44,   0, esc)
            pilula(10, 22, Cores.sol, -31, -31, esc)
            pilula(10, 22, Cores.sol,  31,  31, esc)
            pilula(22, 10, Cores.sol,  31, -31, esc)
            pilula(22, 10, Cores.sol, -31,  31, esc)
        }
    }

    @ViewBuilder
    private func precipitacao(_ esc: CGFloat) -> some View {
        switch nome {
        case "snow":
            ForEach(0 ..< 3, id: \.self) { i in
                disco(12, Cores.lua, -24 + CGFloat(i) * 24, 34, esc)
            }
        case "storm":
            pilula(12, 30, Cores.raio, -4, 30, esc)
            pilula(12, 22, Cores.raio,  8, 42, esc)
        default:   // rain, drizzle, showers, fog — tudo cai aqui, como na placa
            ForEach(0 ..< 3, id: \.self) { i in
                pilula(7, 22, Cores.chuva, -24 + CGFloat(i) * 24, 34, esc)
            }
        }
    }

    /// `nuvem()`: três discos e uma pílula. É a forma que aparece em cinco dos
    /// oito ícones, e por isso é função lá e é função aqui.
    @ViewBuilder
    private func nuvem(_ dx: CGFloat, _ dy: CGFloat, _ esc: CGFloat) -> some View {
        disco(34, Cores.nuvem, dx - 16, dy, esc)
        disco(46, Cores.nuvem, dx + 4, dy - 8, esc)
        disco(30, Cores.nuvem, dx + 24, dy + 2, esc)
        pilula(74, 26, Cores.nuvem, dx + 4, dy + 10, esc)
    }

    /// `disco()`: círculo de diâmetro `d` centrado em (x, y).
    private func disco(_ d: CGFloat, _ cor: Color,
                       _ x: CGFloat, _ y: CGFloat, _ esc: CGFloat) -> some View {
        Circle()
            .fill(cor)
            .frame(width: d * esc, height: d * esc)
            .offset(x: x * esc, y: y * esc)
    }

    /// `barra()`: pílula w×h centrada em (x, y).
    private func pilula(_ w: CGFloat, _ h: CGFloat, _ cor: Color,
                        _ x: CGFloat, _ y: CGFloat, _ esc: CGFloat) -> some View {
        Capsule()
            .fill(cor)
            .frame(width: w * esc, height: h * esc)
            .offset(x: x * esc, y: y * esc)
    }
}

// ————————————————————————————————————————————————
//  Tile 1, o painel de limites
// ————————————————————————————————————————————————

/// A segunda tela da placa: um cartão por limite.
///
/// As medidas fecham para caber QUATRO cartões — 4*96 mais 3*8 de respiro dão
/// 408, dentro dos 420 que sobram quando o rodapé sai de cena. Com três, que é o
/// caso comum, o respiro é 24 e o rodapé fica.
struct TelaLimites: View {
    @ObservedObject var bridge: Bridge

    // criar_painel()
    private static let CARD_X: CGFloat = 24
    private static let CARD_L: CGFloat = 432
    private static let CARD_A: CGFloat = 96
    private static let CARD_GAP: CGFloat = 24
    private static let CARD_GAP_MIN: CGFloat = 8
    private static let CARD_PAD: CGFloat = 14
    private static let BARRA_A: CGFloat = 14      // era 8: a barra é o que se lê de longe
    private static let RITMO_TIQUE_L: CGFloat = 2
    private static let PAINEL_Y0: CGFloat = 46
    private static let PAINEL_Y1: CGFloat = 440
    private static let PAINEL_Y1_CHEIO: CGFloat = 466
    private static let MAX_BARRAS = 4

    private var limites: [Limit] { Array((bridge.data?.limits ?? []).prefix(Self.MAX_BARRAS)) }

    var body: some View {
        GeometryReader { g in
            let v = Vidro(lado: g.size.width)
            let n = limites.count
            // Com quatro limites a conta não fecha com o respiro cheio, e a
            // saída está escrita aqui em vez de escondida num número: o respiro
            // cai para 8 e o rodapé sai de cena para o quarto cartão entrar.
            // Some a informação menos urgente em favor de não esconder um
            // limite inteiro.
            let apertado = n >= Self.MAX_BARRAS
            let gap = apertado ? Self.CARD_GAP_MIN : Self.CARD_GAP
            let y1 = apertado ? Self.PAINEL_Y1_CHEIO : Self.PAINEL_Y1

            // Empilha os cartões CENTRADOS no espaço útil: com três limites um
            // bloco ancorado no topo deixaria um vazio grande sobre o rodapé.
            let alt = CGFloat(n) * Self.CARD_A + CGFloat(max(n - 1, 0)) * gap
            let y0 = max(Self.PAINEL_Y0,
                         Self.PAINEL_Y0 + ((y1 - Self.PAINEL_Y0) - alt) / 2)

            ZStack(alignment: .topLeading) {
                Text("USAGE LIMITS")
                    .font(v.fonte(20, .medium))
                    .foregroundStyle(Cores.titulo)
                    .frame(width: g.size.width, alignment: .center)
                    .offset(y: v.p(16))

                ForEach(Array(limites.enumerated()), id: \.element.id) { i, b in
                    cartao(b, v)
                        .offset(x: v.p(Self.CARD_X),
                                y: v.p(y0 + CGFloat(i) * (Self.CARD_A + gap)))
                }

                if !apertado {
                    frescor(v)
                        .frame(width: g.size.width, alignment: .center)
                        .offset(y: g.size.width - v.p(14 + 20))
                }
            }
        }
    }

    private func cartao(_ b: Limit, _ v: Vidro) -> some View {
        let trilha = Self.CARD_L - 2 * Self.CARD_PAD
        let confiavel = bridge.data?.limitsTrustworthy ?? false

        return ZStack(alignment: .topLeading) {
            // `.circular` e não `.continuous`: o raio do LVGL é um arco de
            // círculo, e o squircle da Apple é visivelmente mais reto no meio da
            // curva. Num cartão de 16px de raio a diferença é pequena e é
            // gratuita de acertar.
            RoundedRectangle(cornerRadius: v.p(16), style: .circular)
                .fill(Cores.cartao)

            // A porcentagem: o maior elemento do cartão, no canto de leitura.
            //
            // DIVERGÊNCIA DELIBERADA — JANELA EXPIRADA.
            // A placa não lê o campo `x` do payload; desenha a porcentagem
            // sempre. Aqui não, e não é capricho de fidelidade: depois de um
            // reset o uso real CAI, então o valor em cache não está só velho,
            // ele erra PARA CIMA e alarma por nada. O travessão diz "não
            // sabemos", que é a verdade. Copiar a placa aqui seria reintroduzir
            // um defeito conhecido para ficar igual.
            Text(b.expired ? "—" : "\(b.p)%")
                .font(v.fonte(32, .semibold))
                .foregroundStyle(Cores.pct)
                .offset(x: v.p(Self.CARD_PAD), y: v.p(4))

            // Pílula ACESA no limite que está valendo, apagada nos outros. A cor
            // da barra diz "quanto já gastei"; a pílula acesa diz "é este que
            // vai te parar primeiro". Duas perguntas, dois canais.
            Text(b.expired ? "expired" : b.l)
                .font(v.fonte(16, .medium))
                .foregroundStyle(b.a ? Cores.pilulaAcesaTexto : Cores.pilulaApagadaTexto)
                .padding(.horizontal, v.p(12))
                .padding(.vertical, v.p(5))
                .background(b.a ? Cores.pilulaAcesaFundo : Cores.pilulaApagadaFundo,
                            in: Capsule())
                .frame(width: v.p(Self.CARD_L - Self.CARD_PAD), alignment: .trailing)
                .offset(y: v.p(14))

            barra(b, trilha: trilha, confiavel: confiavel, v: v)
                .offset(x: v.p(Self.CARD_PAD), y: v.p(46))

            if !b.r.isEmpty && !b.expired {
                Text("resets in \(b.r)")
                    .font(v.fonte(20))
                    .foregroundStyle(Cores.reset)
                    .offset(x: v.p(Self.CARD_PAD), y: v.p(66))
            }
        }
        .frame(width: v.p(Self.CARD_L), height: v.p(Self.CARD_A))
    }

    /// A trilha, o preenchimento, o véu de ritmo e a marca — nesta ordem, que é
    /// a ordem em que a placa os empilha: o indicador é PARTE da barra e sai
    /// antes dos filhos, o véu é filho, e a marca é irmã do véu.
    private func barra(_ b: Limit, trilha: CGFloat,
                       confiavel: Bool, v: Vidro) -> some View {
        let largura = v.p(trilha)
        let altura = v.p(Self.BARRA_A)
        let tique = v.p(Self.RITMO_TIQUE_L)
        // -1 é o sentinela do bridge para janela expirada ou sem tamanho
        // conhecido; e o véu de menos de um tique seria só a marca sentada na
        // ponta arredondada da barra.
        let ritmo = b.e ?? -1
        let veuL = (ritmo >= 0 && ritmo <= 100) ? largura * CGFloat(ritmo) / 100 : -1
        let temVeu = veuL >= tique

        return ZStack(alignment: .leading) {
            Capsule().fill(Cores.trilha)

            if !b.expired {
                Capsule()
                    .fill(Cores.doPct(b.p))
                    // O número velho continua visível, mas a barra — que é a
                    // afirmação mais forte da tela — fala mais baixo.
                    .opacity(confiavel ? 1 : 0.35)
                    .frame(width: max(tique, largura * CGFloat(min(b.p, 100)) / 100))

                if temVeu {
                    // O VÉU DE RITMO: termina onde o preenchimento estaria se a
                    // janela tivesse sido gasta em ritmo constante. Lê-se um
                    // contra o outro — véu sobrando à direita é folga, cor
                    // saturada sobrando à direita do véu é excesso.
                    //
                    // Neutro e não vermelho de propósito: o vermelho já está
                    // gasto no preenchimento acima de 80%, e o ritmo médio é uma
                    // referência, não um alerta.
                    Rectangle()
                        .fill(Cores.veu)
                        .opacity(0.20)
                        .frame(width: veuL)

                    // A MARCA: dois tiques encostados, branco e preto. O branco
                    // sozinho DESAPARECE sobre os preenchimentos claros da faixa
                    // amarela; duas tintas opostas garantem que uma das duas
                    // contraste com o que estiver embaixo.
                    //
                    // Começa um tique ANTES da borda do véu, para o branco cair
                    // sobre o véu e o preto imediatamente fora dele.
                    ZStack(alignment: .leading) {
                        Rectangle().fill(Color.black)
                        Rectangle().fill(Cores.veu).frame(width: tique)
                    }
                    .frame(width: 2 * tique)
                    .offset(x: veuL - tique)
                }
            }
        }
        .frame(width: largura, height: altura)
        // Obrigatório: a barra é uma pílula, e sem isto os cantos quadrados do
        // véu vazariam para fora das pontas. Arredondar o véu seria pior —
        // amaciaria justamente a borda direita que carrega a leitura.
        .clipShape(Capsule())
    }

    /// O rodapé de frescor.
    ///
    /// DIVERGÊNCIA DELIBERADA — A PROCEDÊNCIA DO NÚMERO.
    /// A placa escreve `%d min ago` e nada mais, porque é tudo que ela pode
    /// dizer: o `lim_src` chega até ela e o `ui.c` não o usa. O Mac sabe de onde
    /// o número veio, e a diferença não é decorativa — "cache from 1d" e
    /// "live · 40min" são os dois velhos e não são o mesmo problema. O primeiro
    /// diz que o Claude Code parou de refrescar; o segundo, que nada foi gasto
    /// há um tempo. Então: o formato da placa quando o número é confiável, e a
    /// procedência acrescentada quando não é. A regra da cor é a da placa,
    /// âmbar acima de 15 minutos.
    @ViewBuilder
    private func frescor(_ v: Vidro) -> some View {
        if let d = bridge.data {
            let idade = d.limits_age_s
            let texto: String = {
                if idade < 0 { return "limits unavailable" }
                if d.limitsTrustworthy { return "\(idade / 60) min ago" }
                return "\(d.limitsLive ? "live" : "cache") · \(idade / 60) min ago"
            }()
            Text(texto)
                .font(v.fonte(16))
                .foregroundStyle(idade > 900 || idade < 0
                                 ? Cores.frescorVelho : Cores.frescor)
        }
    }
}

// ————————————————————————————————————————————————
//  A aba
// ————————————————————————————————————————————————

/// As duas telas da placa, empilhadas.
///
/// Quadradas, e é isso que faz a réplica ler como uma tela e não como uma seção
/// do painel: o painel da placa é 480x480 e o vazio em volta dos cartões é parte
/// do desenho — o `posicionar_cards()` centra de propósito. Espremer a altura
/// para "aproveitar o espaço" desmontaria a composição que se quis copiar.
struct AbaPlaca: View {
    @ObservedObject var bridge: Bridge
    let lado: CGFloat

    /// A troca para o repouso, com a régua do bridge — e nenhum prazo escrito
    /// aqui.
    ///
    /// A primeira versão desta propriedade decidia por `sessions.isEmpty` mais um
    /// 300 escrito à mão, e as duas coisas estavam erradas pelo mesmo motivo: o
    /// bridge tira a sessão da lista depois de 30s parada, então "a lista está
    /// vazia" é um limiar de 30 segundos, e o 300 nunca chegava a ser consultado.
    /// O relógio apareceria quatro minutos e meio antes do da placa.
    ///
    /// Não é uma dedução minha — é o defeito que o firmware TEVE, e cujo
    /// conserto está registrado no comentário do `ocioso_bastante` no `ui.c`. A
    /// resposta certa é a mesma dos dois lados: perguntar ao bridge, que é quem
    /// sabe, e é para isso que `idle_age_s` e `rest_s` passaram a viajar no
    /// `/app` também.
    private var emRepouso: Bool {
        guard bridge.state.alive else { return false }
        return bridge.data?.resting ?? false
    }

    var body: some View {
        VStack(spacing: 10) {
            Tela(lado: lado) {
                if emRepouso { TelaRepouso(bridge: bridge) }
                else         { TelaMascote(bridge: bridge) }
            }
            Tela(lado: lado) {
                TelaLimites(bridge: bridge)
            }
        }
    }
}
