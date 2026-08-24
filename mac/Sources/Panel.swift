import SwiftUI

/// State colours — the same ones the mascot uses on the board, so you never
/// have to learn two visual vocabularies for the same information.
///
/// This is the MAC's palette. The board's lives in `Cores`, in Placa.swift, and
/// the two do not mix: one is a set of accents for a translucent panel, the
/// other is a screen painted on black. Mixing them was how the panel ended up
/// with two half-vocabularies instead of one of each.
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

/// O painel, em três abas.
///
/// POR QUE ABAS
/// ------------
/// Havia seis seções numa coluna de 868pt, e a altura era o menor dos
/// problemas: as três primeiras eram barras — uso da janela, limites da
/// assinatura, e mais barras dentro das sessões — desenhadas de três maneiras
/// parecidas e sem nada dizendo que respondiam a perguntas diferentes. Rolar até
/// o fim para achar um checkbox era o sintoma; a causa era não haver separação
/// nenhuma entre olhar o aparelho, olhar o consumo, e mexer na configuração.
///
/// As três abas são essas três coisas, e cada uma cabe sem rolagem.
///
/// SEM CABEÇALHO
/// -------------
/// O painel abria com mascote pequeno + "Wisp" + o estado por extenso. Some: na
/// primeira aba o mascote grande da réplica diz o mesmo com muito mais clareza,
/// e dois mascotes na mesma tela — um de 20px e um de 186 — era exatamente a
/// duplicação que a réplica vem resolver. Fora da primeira aba o estado continua
/// disponível onde já estava antes de o painel abrir: no ícone da barra de menu.
struct Panel: View {
    @ObservedObject var bridge: Bridge

    enum Aba: String, CaseIterable, Identifiable {
        case placa = "Board", uso = "Usage", config = "Settings"
        var id: String { rawValue }
    }

    /// A aba escolhida não persiste entre aberturas do popover, e é de propósito:
    /// o painel abre no aparelho, que é a pergunta que se faz ao abrir. Guardar a
    /// última faria quem ajustou o som uma vez reabrir nos ajustes para sempre.
    @State private var aba: Aba = .placa

    /// A largura do painel. Eram 300; 320 é o que dá à réplica um quadrado de
    /// 292pt, onde as fontes da placa em escala 0,61 ainda se leem — no 300 o
    /// `resets in 3h` do cartão ficava no limite.
    private static let LARGURA: CGFloat = 320
    private static let PAD: CGFloat = 14

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Picker("", selection: $aba) {
                ForEach(Aba.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            switch aba {
            case .placa:
                AbaPlaca(bridge: bridge, lado: Self.LARGURA - 2 * Self.PAD)
            case .uso:
                AbaUso(bridge: bridge)
            case .config:
                AbaConfig(bridge: bridge)
            }
        }
        .padding(Self.PAD)
        .frame(width: Self.LARGURA)
    }
}
