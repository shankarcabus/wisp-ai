import AppKit

/// O som que avisa que o Claude precisa de você.
///
/// SÓ NO MAC, E POR UM MOTIVO DE HARDWARE
/// --------------------------------------
/// A placa C6 não tem áudio utilizável: nenhum `BOARD_HAS_SOUND`, nenhum pino
/// I2S mapeado em nenhum dos dois projetos desta bancada, e o amplificador atrás
/// de um TCA9554 que ninguém dirige. A placa irmã S3 tem um ES8311 com os pinos
/// documentados, e é dela que o projeto vizinho toca som. Aqui, o Mac.
///
/// E é o lugar certo de todo jeito: quando o Claude para e espera por você, é na
/// frente do Mac que você está.
///
/// SONS DO SISTEMA, NADA EMBUTIDO
/// ------------------------------
/// `NSSound(named:)` pega os sons que o macOS já traz. Nada de arquivo de áudio
/// no bundle, nada de dependência nova — e eles já vêm no volume de notificação,
/// que é uma calibragem que ninguém quer refazer.
enum Som {

    /// Mapeamento fixo de estado para som. Fixo, e não configurável, porque
    /// escolher oito sons num painel é trabalho para quem usa e a expressividade
    /// se ganha aqui de graça: alto e curto para quem chama, grave para quem
    /// falhou, cristalino para quem terminou.
    static func nome(_ s: MascotState) -> String {
        switch s {
        case .done:  return "Glass"
        case .error: return "Basso"
        default:     return "Ping"
        }
    }

    /// Toca ao ENTRAR no estado, e é quem chama que garante isso.
    ///
    /// Nunca repetido enquanto o estado dura: um `waiting` de dez minutos toca
    /// uma vez, não seiscentas.
    static func aoEntrar(_ novo: MascotState) {
        guard Ajustes.soundEnabled,
              Ajustes.soundStates.contains(novo.rawValue) else { return }
        NSSound(named: nome(novo))?.play()
    }
}
