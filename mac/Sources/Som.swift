import AppKit

/// O som que avisa que o Claude precisa de você, no Mac.
///
/// A PLACA TAMBÉM TOCA, E TEM A LISTA DELA
/// ---------------------------------------
/// Este arquivo já explicou que a placa C6 não tinha áudio utilizável — sem
/// `BOARD_HAS_SOUND`, sem pinos I2S mapeados, com o amplificador atrás de um
/// TCA9554 que ninguém dirige. Era verdade de OUTRA placa: o expansor é a
/// AMOLED 1.8" C6. Nesta, uma varredura do barramento não acha expansor nenhum e
/// acha o ES8311 em 0x18, e a página do produto anuncia "Audio Playback". A placa
/// toca desde 24/08/2026, pelo I2S em 19/20/22/23, com o amplificador alimentado
/// pelo rail ALDO2 do PMIC.
///
/// Quem decide o som DA PLACA é o firmware, pela lista em `board.sound` que o
/// painel publica — este arquivo cuida apenas do Mac. As duas listas são
/// independentes de propósito: o caso que isso serve é o Mac calado com a placa
/// avisando, e é por isso que a placa nasce muda.
///
/// E o Mac continua sendo um lugar certo: quando o Claude para e espera por você,
/// é na frente do Mac que você está — só não é mais o único que pode falar.
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
    ///
    /// A placa usa este mesmo mapa, com estes mesmos três sons convertidos para
    /// PCM (firmware/tools/sons_para_c.py). Mudar um aqui é mudar em dois
    /// lugares — e é de propósito que sejam os mesmos: o aviso não deve depender
    /// de qual dos dois você ouviu.
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
