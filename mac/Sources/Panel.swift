import SwiftUI
import ServiceManagement

/// State colours — the same ones the mascot uses on the board, so you never
/// have to learn two visual vocabularies for the same information.
enum Palette {
    static func state(_ name: String) -> Color {
        switch name {
        case "working": return Color(red: 0.98, green: 0.55, blue: 0.20)
        case "asking":  return Color(red: 0.65, green: 0.45, blue: 0.95)
        case "waiting": return Color(red: 0.91, green: 0.76, blue: 0.35)
        case "done":    return Color(red: 0.37, green: 0.81, blue: 0.56)
        case "error":   return Color(red: 0.91, green: 0.38, blue: 0.29)
        default:        return Color.secondary
        }
    }

    static func severity(_ s: String) -> Color {
        switch s {
        case "warning":  return Color(red: 0.91, green: 0.76, blue: 0.35)
        case "critical": return Color(red: 0.91, green: 0.38, blue: 0.29)
        default:         return Color(red: 0.37, green: 0.81, blue: 0.56)
        }
    }
}

struct SectionHeader: View {
    let title: String
    var body: some View {
        Text(title.uppercased())
            .font(.system(size: 10, weight: .semibold))
            .foregroundStyle(.tertiary)
            .kerning(0.6)
    }
}

struct LimitBar: View {
    let limit: Limit
    let trustworthy: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                Text(limit.l)
                    .font(.system(size: 11, weight: limit.a ? .semibold : .regular))
                Spacer(minLength: 4)
                // An expired window shows no number.
                //
                // After a reset the real usage DROPS, so the cached value is
                // not merely stale: it errs upward, and alarms you for
                // nothing. A dash says "we do not know" — which is the truth —
                // instead of a number we know to be wrong.
                Text(limit.expired ? "—" : "\(limit.p)%")
                    .font(.system(size: 11, weight: .medium).monospacedDigit())
                    .foregroundStyle(limit.expired || !trustworthy ? .tertiary : .primary)
                Text(limit.expired ? "expired" : limit.r)
                    .font(.system(size: 10))
                    .foregroundStyle(.tertiary)
                    .frame(width: 44, alignment: .trailing)
            }
            GeometryReader { g in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.primary.opacity(0.08))
                    // No bar on an expired window: the bar is the strongest
                    // claim on the screen, and there is nothing to claim.
                    if !limit.expired {
                        Capsule()
                            .fill(Palette.severity(limit.s).opacity(trustworthy ? 1 : 0.35))
                            .frame(width: max(2, g.size.width * CGFloat(limit.p) / 100))
                    }
                }
            }
            .frame(height: 4)
        }
    }
}

struct SessionRow: View {
    let s: Session
    var body: some View {
        HStack(spacing: 7) {
            // One mascot per session, same as the board. A coloured dot would
            // mean memorising a colour code; the character you read directly.
            Mascot(state: MascotState(s.st), side: 20 * Ajustes.macSize.fator)
            Text(s.pj.isEmpty ? "—" : s.pj)
                .font(.system(size: 11, weight: .medium))
                .lineLimit(1)
            Text(s.dt)
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
                .lineLimit(1)
            Spacer(minLength: 0)
            Text("\(s.age)s")
                .font(.system(size: 10).monospacedDigit())
                .foregroundStyle(.tertiary)
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
        // A conta do lado: o painel tem 300px e a Mascot ocupa `side * 1.2` de
        // largura (o quadro externo do SpriteMascot). Oito delas com 3px de
        // respiro em 260px úteis dão 20 de lado, com folga. Com 24 a fileira fica
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

struct Panel: View {
    @ObservedObject var bridge: Bridge
    /// Off only for the documentation shots. The footer is AppKit-backed
    /// (checkboxes, a picker, buttons) and SwiftUI's ImageRenderer cannot draw
    /// NSViews — they come out as placeholder blocks. It is also the part that
    /// says least about what the app does.
    var showsFooter = true
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
        VStack(alignment: .leading, spacing: 12) {
            header

            if let d = bridge.data {
                if d.open_network == true { networkWarning }
                if let w = d.windows, w.ok { windowUsage(w) }
                if !d.limits.isEmpty { limits(d) }
                todayUsage(d)
                sessions(d)
                board(d)
            } else if bridge.state.alive {
                Text("polling the bridge…")
                    .font(.system(size: 11)).foregroundStyle(.secondary)
            }

            if let e = bridge.pollError, bridge.state.alive {
                Text(e).font(.system(size: 10)).foregroundStyle(.red).lineLimit(2)
            }

            if showsFooter {
                Divider()
                footer
            }
        }
        .padding(14)
        .frame(width: 300)
    }

    // MARK: - blocks

    /// The mascot is the first thing in the panel on purpose: the state of your
    /// sessions should be readable before any number, and at a glance.
    private var header: some View {
        HStack(spacing: 10) {
            Mascot(state: bridge.state.alive
                   ? (bridge.data?.dominantState ?? .idle) : .offline,
                   side: 42 * Ajustes.macSize.fator)
            VStack(alignment: .leading, spacing: 1) {
                Text("Wisp").font(.system(size: 13, weight: .semibold))
                Text(bridge.state.alive
                     ? (bridge.data?.dominantState ?? .idle).label
                     : bridge.state.description)
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer()
            if let n = bridge.data?.sessions.count, n > 1 {
                Text("\(n) sessions")
                    .font(.system(size: 10))
                    .foregroundStyle(.tertiary)
            }
        }
    }

    /// Open mode is temporary by nature — it lasts until the board is reflashed
    /// with the token. Without this warning it becomes permanent by neglect,
    /// and the cost is your usage and your project names readable by anyone on
    /// the same network.
    private var networkWarning: some View {
        HStack(alignment: .top, spacing: 6) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.system(size: 10))
                .foregroundStyle(Palette.severity("warning"))
            Text("Open network: anyone on your WiFi can read this data. "
                 + "Reflash the board to close it.")
                .font(.system(size: 10))
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(7)
        .background(Palette.severity("warning").opacity(0.12),
                    in: RoundedRectangle(cornerRadius: 6))
    }

    private func limits(_ d: AppState) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            HStack {
                SectionHeader(title: "Subscription limits")
                Spacer()
                if !d.limitsTrustworthy && d.limits_age_s >= 0 {
                    // The stale number stays visible, but labelled — and the
                    // label names its SOURCE. "cache from 1d" and "live ·
                    // 40min" are both old, and they are not the same problem:
                    // the first says Claude Code stopped refreshing, the
                    // second says nothing has been spent in a while.
                    Text(d.limitsLive ? "live · \(shortAge(d.limits_age_s))"
                                      : "cache from \(shortAge(d.limits_age_s))")
                        .font(.system(size: 9, weight: .medium))
                        .foregroundStyle(Palette.severity("warning"))
                }
            }
            ForEach(d.limits) { LimitBar(limit: $0, trustworthy: d.limitsTrustworthy) }
        }
    }

    /// Usage computed here, from the transcripts. It sits ABOVE the
    /// subscription limits on purpose: this number is always current, that one
    /// depends on a cache Claude Code sometimes lets age for days.
    @ViewBuilder
    private func windowUsage(_ w: Windows) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            SectionHeader(title: "Usage · always current")
            if let s = w.session { band("Session 5h", s) }
            if let s = w.week { band("Week 7d", s) }
            if let days = w.history_d {
                Text("compared to your own peak over \(Int(days)) days")
                    .font(.system(size: 9))
                    .foregroundStyle(.tertiary)
            }
        }
    }

    private func band(_ name: String, _ b: Band) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                Text(name).font(.system(size: 11, weight: .medium))
                Spacer(minLength: 4)
                Text("\(b.reqs) reqs · \(compact(b.output))")
                    .font(.system(size: 10).monospacedDigit())
                    .foregroundStyle(.secondary)
                if b.comparable {
                    Text("\(b.pct)%")
                        .font(.system(size: 11, weight: .semibold).monospacedDigit())
                        .frame(width: 34, alignment: .trailing)
                }
            }
            if b.comparable {
                GeometryReader { g in
                    ZStack(alignment: .leading) {
                        Capsule().fill(Color.primary.opacity(0.08))
                        Capsule()
                            .fill(Palette.state("working"))
                            .frame(width: max(2, g.size.width
                                              * CGFloat(min(b.pct, 100)) / 100))
                    }
                }
                .frame(height: 4)
            }
        }
    }

    private func todayUsage(_ d: AppState) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            SectionHeader(title: "Today")
            HStack(spacing: 14) {
                metric("\(d.usage.requests ?? 0)", "requests")
                metric(compact(d.usage.output), "output")
                metric(compact(d.usage.cache_read), "cache")
            }
        }
    }

    private func metric(_ value: String, _ name: String) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(value).font(.system(size: 14, weight: .semibold).monospacedDigit())
            Text(name).font(.system(size: 9)).foregroundStyle(.tertiary)
        }
    }

    private func sessions(_ d: AppState) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            SectionHeader(title: d.sessions.isEmpty
                          ? "No active sessions" : "Active sessions")
            ForEach(d.sessions) { SessionRow(s: $0) }
        }
    }

    private func board(_ d: AppState) -> some View {
        HStack(spacing: 7) {
            Circle()
                .fill(d.boardAlive ? Palette.state("done") : Color.secondary.opacity(0.4))
                .frame(width: 7, height: 7)
            Text("Waveshare").font(.system(size: 11, weight: .medium))
            /// Only shows up when the board is on battery and reported a
            /// charge. On the cable, with a full cell, the number helps decide
            /// nothing.
            if let pct = d.batteryPct {
                Text(d.batteryCharging ? "\(pct)% ⚡" : "\(pct)%")
                    .font(.system(size: 10, weight: d.batteryLow ? .semibold : .regular))
                    .foregroundStyle(d.batteryLow ? Palette.state("error") : .secondary)
            }
            Spacer()
            Text(d.board_age_s < 0 ? "never showed up"
                 : d.boardAlive ? d.board_ip
                 : "gone for \(shortAge(d.board_age_s))")
                .font(.system(size: 10))
                .foregroundStyle(.secondary)
        }
    }

    private var footer: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle(isOn: $openAtLogin) {
                Text("Open at login").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .onChange(of: openAtLogin) { _, on in applyLogin(on) }

            SectionHeader(title: "Mascot")
                .padding(.top, 2)

            Galeria(personagem: Sprites.chosen)

            if !Sprites.available().isEmpty {
                Picker("Character",
                       selection: liga({ Sprites.chosen }, { Sprites.chosen = $0 })) {
                    Text("Wisp (vector)").tag("")
                    ForEach(Sprites.available(), id: \.self) { Text($0).tag($0) }
                }
                .font(.system(size: 11))
            }

            Toggle(isOn: liga({ Ajustes.boardAction }, { Ajustes.boardAction = $0 })) {
                Text("Action under the mascot").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("On the board. In Portuguese the label shows the state, "
                  + "because the tool name it would show instead comes from the "
                  + "bridge in English and cannot be translated.")

            Toggle(isOn: liga({ Ajustes.boardProject }, { Ajustes.boardProject = $0 })) {
                Text("Projects under the mascot").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("On the board. The list of running sessions, one line each.")

            Picker("Language",
                   selection: liga({ Ajustes.boardLanguage }, { Ajustes.boardLanguage = $0 })) {
                Text("English").tag("en")
                Text("Português").tag("pt")
            }
            .font(.system(size: 11))

            Picker("Size on the board",
                   selection: liga({ Ajustes.boardSize }, { Ajustes.boardSize = $0 })) {
                ForEach(Ajustes.Tamanho.allCases, id: \.self) { Text($0.rotulo).tag($0) }
            }
            .font(.system(size: 11))

            Picker("Size on the Mac",
                   selection: liga({ Ajustes.macSize }, { Ajustes.macSize = $0 })) {
                ForEach(Ajustes.Tamanho.allCases, id: \.self) { Text($0.rotulo).tag($0) }
            }
            .font(.system(size: 11))

            Toggle(isOn: liga({ Ajustes.soundEnabled }, { Ajustes.soundEnabled = $0 })) {
                Text("Sound when the state changes").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("On the Mac. The C6 board has no usable audio — no I2S pins "
                  + "mapped and the amplifier behind an expander nobody drives.")

            if Ajustes.soundEnabled {
                CaixasDeSom(estados: liga({ Ajustes.soundStates },
                                          { Ajustes.soundStates = $0 }))
                    .padding(.leading, 18)
            }

            Divider()

            Toggle(isOn: $bridge.floating) {
                Text("Mascot on the desktop").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("Leaves the mascot loose on screen, always visible. "
                  + "Drag it to position it.")

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
