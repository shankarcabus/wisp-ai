import Foundation

/// Os ajustes que o painel publica, e o único lugar que conhece o formato.
///
/// UserDefaults continua sendo a loja do app — é dele que a interface lê e é nele
/// que o SwiftUI observa. O `~/.wisp/ui.json` é a versão PUBLICADA, para o bridge
/// e a placa: um arquivo, porque `defaults read` do lado do Python passa pelo
/// cfprefsd, e leitura defasada ali é o tipo de bug que custa uma tarde sem se
/// anunciar.
///
/// Substitui o `~/.wisp/mascot` de uma linha. O bridge continua LENDO o antigo
/// como migração, para instalações que ainda o tenham — ninguém roda nada.
enum Ajustes {

    enum Tamanho: String, CaseIterable {
        case small, medium, large

        /// Fator sobre os tamanhos de hoje. `medium` é exatamente o que existe,
        /// para quem nunca mexer não ver diferença nenhuma.
        var fator: CGFloat {
            switch self {
            case .small:  return 0.8
            case .medium: return 1.0
            case .large:  return 1.25
            }
        }
        var rotulo: String { rawValue }
    }

    // Prefixadas, para não colidir com as chaves que já existem no UserDefaults
    // deste app: `mascot`, `floating`, `fetchLimits`.
    static let kBoardSize    = "ui.board.size"
    static let kBoardAction  = "ui.board.action_label"
    static let kBoardProject = "ui.board.project_label"
    static let kBoardLang    = "ui.board.language"
    static let kMacSize      = "ui.mac.size"
    static let kSoundOn      = "ui.mac.sound.enabled"
    static let kSoundStates  = "ui.mac.sound.states"

    // ── Os acessores ──
    //
    // Três helpers em vez de sete corpos parecidos. O que importa aqui não é a
    // economia de linhas: é que "gravar E publicar" estava escrito sete vezes, e
    // um ajuste novo que esquecesse o publicar() daria um painel que parece certo
    // e uma placa que nunca muda, sem nada falhando. Agora esquecer é impossível
    // — não há como gravar sem passar por `gravar`.
    //
    // Os GETTERS continuam separados porque as quatro formas de valor são
    // genuinamente diferentes (enum, Bool com padrão, String, Set), e forçá-las
    // num acessor genérico esconderia o padrão de cada uma.

    private static func gravar(_ v: Any, _ chave: String) {
        UserDefaults.standard.set(v, forKey: chave)
        publicar()
    }
    private static func bool(_ chave: String, _ padrao: Bool) -> Bool {
        UserDefaults.standard.object(forKey: chave) as? Bool ?? padrao
    }
    private static func tamanho(_ chave: String) -> Tamanho {
        Tamanho(rawValue: UserDefaults.standard.string(forKey: chave) ?? "") ?? .medium
    }

    static var boardSize: Tamanho {
        get { tamanho(kBoardSize) }
        set { gravar(newValue.rawValue, kBoardSize) }
    }
    static var macSize: Tamanho {
        get { tamanho(kMacSize) }
        set { gravar(newValue.rawValue, kMacSize) }
    }
    static var boardAction: Bool {
        get { bool(kBoardAction, true) }
        set { gravar(newValue, kBoardAction) }
    }
    static var boardProject: Bool {
        get { bool(kBoardProject, true) }
        set { gravar(newValue, kBoardProject) }
    }
    static var boardLanguage: String {
        get { UserDefaults.standard.string(forKey: kBoardLang) ?? "en" }
        set { gravar(newValue, kBoardLang) }
    }
    static var soundEnabled: Bool {
        get { bool(kSoundOn, true) }
        set { gravar(newValue, kSoundOn) }
    }

    /// Em quais estados tocar. Padrão nos dois que significam "o Claude está te
    /// esperando" — os outros seis viram ruído, e ruído se desliga uma vez e
    /// nunca mais se liga.
    static var soundStates: Set<String> {
        get {
            guard let a = UserDefaults.standard.array(forKey: kSoundStates) as? [String]
            else { return ["asking", "waiting"] }
            return Set(a)
        }
        set { gravar(newValue.sorted(), kSoundStates) }
    }

    /// Onde o JSON é publicado.
    ///
    /// Var, e isto NÃO é conveniência de teste: qualquer ferramenta que mexa em
    /// `Sprites.chosen` publica por consequência, porque o setter dele chama
    /// `publicar()`. Uma ferramenta de bancada que só queria renderizar a partir
    /// de outra pasta de arte acaba reescrevendo a configuração de quem a rodou
    /// — aconteceu com o `mac/shots.sh`, que ficou com `character: "assets"` no
    /// arquivo de alguém. Toda ferramenta que redireciona `Sprites.folder` tem de
    /// redirecionar isto também.
    static var arquivo = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent(".wisp/ui.json")

    /// Escreve o JSON inteiro, por temp+rename, para quem lê nunca ver meio
    /// arquivo. Chamado por cada setter: este app não tem botão de salvar.
    static func publicar() {
        let payload: [String: Any] = [
            "character": Sprites.chosen,
            "board": ["size": boardSize.rawValue,
                      "action_label": boardAction,
                      "project_label": boardProject,
                      "language": boardLanguage],
            "mac": ["size": macSize.rawValue,
                    "sound": ["enabled": soundEnabled,
                              "states": soundStates.sorted()]],
        ]
        let dir = arquivo.deletingLastPathComponent()
        let tmp = arquivo.appendingPathExtension("tmp")
        do {
            try FileManager.default.createDirectory(at: dir,
                                                    withIntermediateDirectories: true)
            let dados = try JSONSerialization.data(withJSONObject: payload,
                                                   options: [.prettyPrinted, .sortedKeys])
            try dados.write(to: tmp)
            _ = try FileManager.default.replaceItemAt(arquivo, withItemAt: tmp)
        } catch {
            // Falhar aqui não pode derrubar a mudança no Mac: o mascote da barra
            // já mudou, e o que se perde é a placa acompanhar.
            NSLog("wisp: could not publish the settings: \(error.localizedDescription)")
        }
    }
}
