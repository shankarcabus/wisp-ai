import SwiftUI
import ServiceManagement

/// Os ajustes, agrupados por QUEM eles afetam.
///
/// Eram catorze controles numa coluna plana no pé do painel, na ordem em que
/// foram sendo escritos. A ordem custava: "Size on the board" e "Size on the
/// Mac" eram vizinhos e quase idênticos, então a pergunta "qual destes dois eu
/// mexo?" tinha de ser respondida lendo o rótulo com atenção, toda vez. Um
/// grupo chamado *On the board* responde antes de a pergunta ser feita.
///
/// Os quatro grupos são as quatro respostas possíveis: o personagem (que vale
/// nos dois lados), a placa, o Mac, e o app. Nada aqui mudou de comportamento —
/// os controles são os mesmos, ligados nos mesmos lugares.
struct AbaConfig: View {
    @ObservedObject var bridge: Bridge

    @State private var openAtLogin = SMAppService.mainApp.status == .enabled
    @State private var loginError: String?

    /// Contador de revisão: a única coisa que o SwiftUI precisa observar para
    /// reavaliar o painel quando um ajuste muda.
    ///
    /// Antes cada ajuste tinha um @State que copiava o valor do `Ajustes` na
    /// construção da View, pareado à mão com um `.onChange` que o escrevia de
    /// volta. Sete cópias e sete pares. O risco não era o tamanho: um `.onChange`
    /// esquecido dá um controle que se move na tela e não persiste, sem nada
    /// falhando — e um ajuste novo exigia três edições coordenadas.
    @State private var revisao = 0

    /// Liga um controle direto ao `Ajustes`, sem estado intermediário. A leitura
    /// vai à fonte; a escrita vai à fonte e pede uma reavaliação.
    private func liga<T>(_ ler: @escaping () -> T,
                         _ gravar: @escaping (T) -> Void) -> Binding<T> {
        Binding(get: ler, set: { gravar($0); revisao += 1 })
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            personagem
            naPlaca
            noMac
            app
        }
    }

    // MARK: - grupos

    private var personagem: some View {
        Bloco("Character") {
            Galeria(personagem: Sprites.chosen)

            if !Sprites.available().isEmpty {
                Campo("Set") {
                    Picker("", selection: liga({ Sprites.chosen },
                                               { Sprites.chosen = $0 })) {
                        Text("Wisp (vector)").tag("")
                        ForEach(Sprites.available(), id: \.self) { Text($0).tag($0) }
                    }
                }
            }
        }
    }

    private var naPlaca: some View {
        Bloco("On the board") {
            Toggle(isOn: liga({ Ajustes.boardAction }, { Ajustes.boardAction = $0 })) {
                Text("Action under the mascot").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("In Portuguese the label shows the state, because the tool "
                  + "name it would show instead comes from the bridge in "
                  + "English and cannot be translated.")

            Toggle(isOn: liga({ Ajustes.boardProject }, { Ajustes.boardProject = $0 })) {
                Text("Projects under the mascot").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("The list of running sessions, one line each.")

            Campo("Language") {
                Picker("", selection: liga({ Ajustes.boardLanguage },
                                           { Ajustes.boardLanguage = $0 })) {
                    Text("English").tag("en")
                    Text("Português").tag("pt")
                }
            }

            Campo("Mascot size") {
                Picker("", selection: liga({ Ajustes.boardSize },
                                           { Ajustes.boardSize = $0 })) {
                    ForEach(Ajustes.Tamanho.allCases, id: \.self) {
                        Text($0.rotulo).tag($0)
                    }
                }
            }

            Toggle(isOn: liga({ Ajustes.boardSoundEnabled },
                              { Ajustes.boardSoundEnabled = $0 })) {
                Text("Sound when the state changes").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("The board has its own list, independent from the Mac's: "
                  + "the case this serves is a silent Mac with the board "
                  + "calling you. Off by default, so updating never starts "
                  + "playing the same alert twice.")

            if Ajustes.boardSoundEnabled {
                CaixasDeSom(estados: liga({ Ajustes.boardSoundStates },
                                          { Ajustes.boardSoundStates = $0 }))
                    .padding(.leading, 18)

                Campo("Volume") {
                    Picker("", selection: liga({ Ajustes.boardVolume },
                                               { Ajustes.boardVolume = $0 })) {
                        ForEach(Ajustes.Volume.allCases, id: \.self) {
                            Text($0.rotulo).tag($0)
                        }
                    }
                }
            }
        }
    }

    private var noMac: some View {
        Bloco("On the Mac") {
            Campo("Mascot size") {
                Picker("", selection: liga({ Ajustes.macSize },
                                           { Ajustes.macSize = $0 })) {
                    ForEach(Ajustes.Tamanho.allCases, id: \.self) {
                        Text($0.rotulo).tag($0)
                    }
                }
            }

            Toggle(isOn: $bridge.floating) {
                Text("Mascot on the desktop").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("Leaves the mascot loose on screen, always visible. "
                  + "Drag it to position it.")

            Toggle(isOn: liga({ Ajustes.soundEnabled }, { Ajustes.soundEnabled = $0 })) {
                Text("Sound when the state changes").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("Independent from the board's list, right above — "
                  + "each side has its own states.")

            if Ajustes.soundEnabled {
                CaixasDeSom(estados: liga({ Ajustes.soundStates },
                                          { Ajustes.soundStates = $0 }))
                    .padding(.leading, 18)
            }
        }
    }

    private var app: some View {
        Bloco("App") {
            Toggle(isOn: $openAtLogin) {
                Text("Open at login").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .onChange(of: openAtLogin) { _, on in applyLogin(on) }

            Toggle(isOn: $bridge.fetchLimits) {
                Text("Fetch real limits").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("Asks for keychain access to read the limits straight from "
                  + "Anthropic, instead of Claude Code's cache, which "
                  + "sometimes goes days without refreshing.")

            if let e = bridge.limitsError {
                Text(e).font(.system(size: 9)).foregroundStyle(.secondary).lineLimit(2)
            }

            if let e = loginError {
                Text(e).font(.system(size: 9)).foregroundStyle(.red).lineLimit(2)
            }

            HStack {
                Button(bridge.state.alive ? "Stop bridge" : "Start bridge") {
                    bridge.toggle()
                }
                .font(.system(size: 11))
                Spacer()
                Button("Quit") { NSApplication.shared.terminate(nil) }
                    .font(.system(size: 11))
            }
            .padding(.top, 2)
        }
    }

    private func applyLogin(_ on: Bool) {
        do {
            if on { try SMAppService.mainApp.register() }
            else  { try SMAppService.mainApp.unregister() }
            loginError = nil
        } catch {
            // Common failure: unsigned app, or one outside /Applications.
            // We say why instead of letting the checkbox lie.
            loginError = "did not work: \(error.localizedDescription)"
            openAtLogin = SMAppService.mainApp.status == .enabled
        }
    }
}

/// Um ajuste com rótulo à esquerda e controle à direita, o rótulo numa largura
/// FIXA.
///
/// Sem isto cada `Picker` mede o próprio rótulo e o controle começa onde o texto
/// acabou: "Set", "Language" e "Mascot size" davam três recuos diferentes e a
/// coluna de controles saía serrilhada — que é o mesmo tipo de desalinho que o
/// resto desta reorganização veio desfazer.
///
/// Um `Form` alinharia isto sozinho, e traria o estilo inteiro do Form junto:
/// dentro de um popover de 292pt de largura ele impõe recuos e uma tipografia que
/// não são os do painel. 76pt cabem o mais largo dos quatro rótulos ("Mascot
/// size" mede 64) e deixam 208 para o controle.
private struct Campo<Controle: View>: View {
    let rotulo: String
    @ViewBuilder var controle: Controle

    init(_ rotulo: String, @ViewBuilder controle: () -> Controle) {
        self.rotulo = rotulo
        self.controle = controle()
    }

    var body: some View {
        HStack(spacing: 8) {
            Text(rotulo)
                .font(.system(size: 11))
                .frame(width: 76, alignment: .leading)
            controle
                .labelsHidden()
                .font(.system(size: 11))
        }
    }
}

/// Um grupo de ajustes com título.
///
/// O título é o que faz o agrupamento existir para quem lê — sem ele o
/// espaçamento maior entre blocos seria só espaçamento irregular.
private struct Bloco<Conteudo: View>: View {
    let titulo: String
    @ViewBuilder var conteudo: Conteudo

    init(_ titulo: String, @ViewBuilder conteudo: () -> Conteudo) {
        self.titulo = titulo
        self.conteudo = conteudo()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            SectionHeader(title: titulo)
            conteudo
        }
    }
}

/// Os oito estados do personagem escolhido, em fileira.
///
/// Sai de graça: os sprites já estão carregados para desenhar o estado atual, e
/// mostrar os oito é o que responde "não consigo ver todos os estados". Escolher
/// personagem sem isso é escolher no escuro.
///
/// Sem rótulo por estado — oito palavras em 300px de largura viram ruído, e o que
/// se quer aqui é reconhecer a cara. O nome vem no tooltip.
struct Galeria: View {
    /// O nome do personagem. Não é usado no corpo — é usado por EXISTIR.
    ///
    /// Sem nenhuma propriedade, esta View é sempre o mesmo valor, e o SwiftUI
    /// pode não reavaliar o corpo dela quando o resto do painel muda. O
    /// resultado é uma fileira congelada nas imagens carregadas quando o painel
    /// abriu: trocar de personagem no seletor mudava o mascote do cabeçalho, que
    /// depende do `bridge` observado, e não mudava a galeria.
    ///
    /// Passar o nome dá à View uma identidade que muda junto com a escolha, e é
    /// isso que faz o corpo ser reavaliado e o `Sprites.image` ser consultado de
    /// novo — com o cache já limpo pelo setter de `Sprites.chosen`.
    let personagem: String

    var body: some View {
        // A conta do lado: o painel tem 320px e a Mascot ocupa `side * 1.2` de
        // largura (o quadro externo do SpriteMascot). Oito delas com 3px de
        // respiro em 280px úteis dão 20 de lado, com folga. Com 24 a fileira fica
        // mais larga que o painel e EMPURRA o resto do conteúdo para fora — o
        // HStack cresce e o popover cresce com ele.
        HStack(spacing: 3) {
            ForEach(MascotState.allCases, id: \.self) { s in
                Mascot(state: s, side: 20)
                    .help(s.label)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// Em quais estados tocar som.
///
/// Oito caixas em duas fileiras de quatro. Rótulo pelo nome CRU do estado
/// (`asking`, `waiting`) e não pelo texto amigável ("needs you"): oito frases em
/// 300px não caberiam, e aqui o que se quer é marcar, não ler.
///
/// Recebe o conjunto por Binding porque a fonte da verdade é o `Ajustes`, e um
/// @State local por caixa seria oito lugares guardando a mesma coisa.
struct CaixasDeSom: View {
    @Binding var estados: Set<String>

    /// As fileiras saem de `allCases`, em blocos de quatro. Estavam escritas à
    /// mão, o que fazia um estado novo aparecer na galeria (que já deriva de
    /// `allCases`) e faltar aqui — silenciosamente impossível de ligar.
    private var linhas: [[MascotState]] {
        stride(from: 0, to: MascotState.allCases.count, by: 4).map { i in
            Array(MascotState.allCases[i ..< min(i + 4, MascotState.allCases.count)])
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(linhas.indices, id: \.self) { i in
                HStack(spacing: 8) {
                    ForEach(linhas[i], id: \.self) { s in
                        Toggle(isOn: Binding(
                            get: { estados.contains(s.rawValue) },
                            set: { on in
                                if on { estados.insert(s.rawValue) }
                                else  { estados.remove(s.rawValue) }
                            }
                        )) {
                            Text(s.rawValue).font(.system(size: 10))
                        }
                        .toggleStyle(.checkbox)
                        .frame(width: 62, alignment: .leading)
                    }
                }
            }
        }
    }
}
