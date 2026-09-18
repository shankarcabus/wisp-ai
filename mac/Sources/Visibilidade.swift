import SwiftUI
import AppKit

/// Quando um mascote tem permissão para gastar frames.
///
/// POR QUE ISTO EXISTE
/// -------------------
/// `TimelineView(.animation)` está preso ao display link e não sabe nada sobre
/// janelas: pede um quadro por refresh e continua pedindo depois que a janela
/// saiu da tela. O `MenuBarExtra` não destrói o painel quando ele fecha — apenas
/// o retira da tela, com o `@State` da aba parado onde você o deixou. As duas
/// coisas juntas dão um painel fechado que desenha para sempre.
///
/// MEDIDO, NÃO ASSUMIDO. Um `TimelineView(.animation)` numa janela levada a
/// `orderOut`: 126 fps na tela, 126 fps escondida. Nada cai sozinho. Com a sonda
/// abaixo: 0,3 fps escondida, e os mesmos 126 ao reaparecer.
///
/// O que isso custava no app: largado na aba Settings, o painel tem OITO
/// mascotes na galeria, cada um com seu `TimelineView`. São oito animações por
/// refresh desenhadas para ninguém, e foi o que manteve o Wisp numa média de
/// 14,6% de CPU ao longo de dez dias de uptime — com zero janelas na tela.
///
/// A placa chegou à mesma conclusão pelo outro lado, e ela está no CLAUDE.md
/// como "not a bug": manter o frame loop vivo com arte em imagem reinvalidava a
/// imagem inteira a cada quadro. Aqui era o mesmo erro, do lado do Mac.
///
/// O padrão é `true`, então quem não instala a sonda — `Preview`, `Shots` —
/// anima exatamente como sempre animou.
private struct MascoteAnimadoKey: EnvironmentKey {
    static let defaultValue = true
}

extension EnvironmentValues {
    var mascoteAnimado: Bool {
        get { self[MascoteAnimadoKey.self] }
        set { self[MascoteAnimadoKey.self] = newValue }
    }
}

extension View {
    /// Liga a animação dos mascotes abaixo desta view à visibilidade real da
    /// janela que os contém. Vai na raiz de cada janela: o painel, e o mascote
    /// solto na mesa.
    func seguindoAVisibilidadeDaJanela() -> some View {
        modifier(SegueAVisibilidade())
    }

    /// Congela os mascotes abaixo desta view mesmo com a janela na tela.
    ///
    /// Para fileiras onde o movimento não informa nada: a galeria dos oito
    /// estados é um SELETOR, e oito bonecos respirando juntos custam oito vezes
    /// o mesmo quadro para dizer o que uma pose parada já diz.
    func semAnimacao() -> some View {
        environment(\.mascoteAnimado, false)
    }
}

private struct SegueAVisibilidade: ViewModifier {
    /// Começa em `true` para a primeira abertura não nascer congelada: a sonda
    /// corrige no `viewDidMoveToWindow`, que acontece antes do primeiro quadro.
    @State private var visivel = true

    func body(content: Content) -> some View {
        content
            .environment(\.mascoteAnimado, visivel)
            .background(Sonda(visivel: $visivel).frame(width: 0, height: 0))
    }
}

/// Publica se a janela desta view está de fato diante de alguém.
///
/// Dois sinais, porque um só não cobre os dois casos: `isVisible` cai quando a
/// janela é retirada da tela — que é o que o popover do `MenuBarExtra` faz ao
/// fechar — e `occlusionState` cai quando ela continua aberta e totalmente
/// coberta por outra janela. Os dois foram vistos disparar em banco.
private struct Sonda: NSViewRepresentable {
    @Binding var visivel: Bool

    func makeNSView(context: Context) -> NSView { Vista(visivel: $visivel) }
    func updateNSView(_ nsView: NSView, context: Context) {}

    final class Vista: NSView {
        @Binding private var visivel: Bool
        private var kvo: NSKeyValueObservation?

        init(visivel: Binding<Bool>) {
            _visivel = visivel
            super.init(frame: .zero)
        }

        required init?(coder: NSCoder) { fatalError("init(coder:) não é usado aqui") }

        deinit { NotificationCenter.default.removeObserver(self) }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            // A view muda de janela mais de uma vez na vida do painel; sem
            // limpar, os observadores se acumulam sobre janelas mortas.
            NotificationCenter.default.removeObserver(self)
            kvo = nil

            guard let janela = window else { return publica(false) }

            NotificationCenter.default.addObserver(
                self, selector: #selector(reavalia),
                name: NSWindow.didChangeOcclusionStateNotification, object: janela)
            kvo = janela.observe(\.isVisible) { [weak self] _, _ in
                MainActor.assumeIsolated { self?.reavalia() }
            }
            reavalia()
        }

        @objc private func reavalia() {
            guard let janela = window else { return publica(false) }
            publica(janela.isVisible && janela.occlusionState.contains(.visible))
        }

        /// A guarda não é economia de linha: escrever o mesmo valor num
        /// `@Binding` reavalia a árvore, e reavaliar a árvore à toa é
        /// exatamente o que este arquivo veio evitar.
        private func publica(_ novo: Bool) {
            guard visivel != novo else { return }
            visivel = novo
        }
    }
}
