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

    static var boardSize: Tamanho {
        get { Tamanho(rawValue: UserDefaults.standard.string(forKey: kBoardSize) ?? "") ?? .medium }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: kBoardSize); publicar() }
    }
    static var macSize: Tamanho {
        get { Tamanho(rawValue: UserDefaults.standard.string(forKey: kMacSize) ?? "") ?? .medium }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: kMacSize); publicar() }
    }
    static var boardAction: Bool {
        get { UserDefaults.standard.object(forKey: kBoardAction) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: kBoardAction); publicar() }
    }
    static var boardProject: Bool {
        get { UserDefaults.standard.object(forKey: kBoardProject) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: kBoardProject); publicar() }
    }
    static var boardLanguage: String {
        get { UserDefaults.standard.string(forKey: kBoardLang) ?? "en" }
        set { UserDefaults.standard.set(newValue, forKey: kBoardLang); publicar() }
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
            "mac": ["size": macSize.rawValue],
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
