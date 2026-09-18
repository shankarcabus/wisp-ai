import SwiftUI
import AppKit

/// Loads image-based mascots from ~/.wisp/mascots/<name>/.
///
/// It exists so the character stops being hostage to what I can draw in code.
/// Procedural vectors scale well and weigh nothing, but they have a ceiling:
/// they do not reach the finish of a 3D render. With this, any art can come in
/// — hand-drawn, commissioned or generated — and the vector becomes the
/// factory default.
///
/// RULES
/// -----
/// One PNG per state, named the way the bridge names the states. If any one is
/// missing, the whole set is ignored and we fall back to the vector: a
/// coherent character beats seven pretty frames and a hole.
///
/// The images are static. The motion comes from the code — floating,
/// squashing, tilting. On a 46px character on the desktop, frame-by-frame
/// animation would be invisible work.
enum Sprites {

    /// A var, not a let, for one reason: mac/shots.sh points it at the
    /// repository's own firmware/assets so the documentation images render
    /// from art that ships here, instead of from whatever happens to be
    /// installed on the machine that generated them.
    static var folder = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent(".wisp/mascots", isDirectory: true)

    /// The state's name as a file name. These are the same names that travel
    /// through /state, so the project has no second translation table.
    static func file(_ s: MascotState) -> String {
        switch s {
        case .idle:     return "idle"
        case .working:  return "working"
        case .tool:     return "tool"
        case .asking:   return "asking"
        case .waiting:  return "waiting"
        case .done:     return "done"
        case .error:    return "error"
        case .offline:  return "offline"
        }
    }

    /// Which set to use. Empty = the built-in vector.
    static var chosen: String {
        get {
            let d = UserDefaults.standard
            if let n = d.string(forKey: "mascot") { return n }
            // Key from before the Fagulha -> Wisp rename.
            //
            // Renaming the key silently discarded the choice: the picker fell
            // back to the built-in vector and nothing said why. Same shape as
            // the NVS namespace on the board — renames are consistent inside
            // the repo and blind to state stored outside it.
            //
            // Adopt it once, then write under the new name so this path stops
            // being taken.
            if let previous = d.string(forKey: "mascote"), !previous.isEmpty {
                d.set(previous, forKey: "mascot")
                return previous
            }
            // Nothing chosen yet: prefer the Terminal art when it is there.
            // install.sh puts it in place from firmware/assets, so the Mac
            // shows the same character as the board out of the box — one
            // character for the whole project, and the one the README shows.
            // The vector stays as the fallback, not as the default.
            if isComplete("terminal") { return "terminal" }
            return ""
        }
        set {
            UserDefaults.standard.set(newValue, forKey: "mascot")
            cache.removeAll()
            complete.removeAll()
            Ajustes.publicar()
        }
    }

    /// Available sets: subfolders that carry all eight states.
    static func available() -> [String] {
        guard let items = try? FileManager.default.contentsOfDirectory(
            at: folder, includingPropertiesForKeys: [.isDirectoryKey]) else { return [] }
        return items
            .filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory == true }
            .map { $0.lastPathComponent }
            .filter { isComplete($0) }
            .sorted()
    }

    private static var cache: [String: NSImage] = [:]
    private static var complete: [String: Bool] = [:]

    /// A set only counts if it has EVERY state.
    static func isComplete(_ name: String) -> Bool {
        if let c = complete[name] { return c }
        let base = folder.appendingPathComponent(name, isDirectory: true)
        let ok = MascotState.allCases.allSatisfy { s in
            ["png", "PNG"].contains { ext in
                FileManager.default.fileExists(
                    atPath: base.appendingPathComponent("\(file(s)).\(ext)").path)
            }
        }
        complete[name] = ok
        return ok
    }

    static func image(_ s: MascotState) -> Image? {
        let name = chosen
        guard !name.isEmpty, isComplete(name) else { return nil }
        let key = "\(name)/\(file(s))"
        if let img = cache[key] { return Image(nsImage: img) }

        let base = folder.appendingPathComponent(name, isDirectory: true)
        for ext in ["png", "PNG"] {
            let url = base.appendingPathComponent("\(file(s)).\(ext)")
            if let img = NSImage(contentsOf: url) {
                cache[key] = img
                return Image(nsImage: img)
            }
        }
        return nil
    }
}

/// The image mascot, with its motion coming from the code.
///
/// The same gestures as the vector — breathing, squash and stretch, a curious
/// tilt — so both paths read as the same character in temperament, not just in
/// shape.
struct SpriteMascot: View {
    let state: MascotState
    let image: Image
    var side: CGFloat = 64

    /// Ver Visibilidade.swift. Fora da tela — ou numa fileira que é seletor — o
    /// movimento não chega a ninguém, e o quadro é desperdício puro.
    @Environment(\.mascoteAnimado) private var animado

    @ViewBuilder
    var body: some View {
        if animado {
            TimelineView(.animation) { ctx in
                quadro(ctx.date.timeIntervalSinceReferenceDate)
            }
            .accessibilityLabel("mascot: \(state.label)")
        } else {
            quadro(nil)
                .accessibilityLabel("mascot: \(state.label)")
        }
    }

    /// Um quadro do personagem. `t` nulo é a pose de REPOUSO — escala 1, sem
    /// subida, sem inclinação — e não um instante qualquer congelado: parar num
    /// `t` arbitrário pega o personagem no meio de um suspiro, torto.
    private func quadro(_ t: Double?) -> some View {
        // Squash & stretch: volume is conserved, so whatever stretches
        // vertically shrinks horizontally. Without it the character just
        // "inflates".
        let s = t.map { 1 + sin($0 / state.period * 2 * .pi) * state.breath } ?? 1
        // Floating follows the breathing, half a cycle behind — the body
        // rises after filling up, the way it really happens.
        let rise = t.map { CGFloat(sin($0 / state.period * 2 * .pi - 0.9)) * side * 0.03 } ?? 0
        // Curiosity and distress tilt the head; work does not.
        let tilt: Double = {
            switch state {
            case .asking:  return t.map { sin($0 * 1.6) * 5 } ?? 0
            case .waiting: return t.map { sin($0 * 2.4) * 3 } ?? 0
            // O -4 do erro é POSE, não movimento: zerá-lo na fileira parada
            // tiraria justamente a expressão que distingue o estado.
            case .error:   return -4
            default:       return 0
            }
        }()

        return image
            .resizable()
            .interpolation(.high)
            .aspectRatio(contentMode: .fit)
            .frame(width: side, height: side)
            .scaleEffect(x: 1 / s, y: s, anchor: .bottom)
            .rotationEffect(.degrees(tilt), anchor: .bottom)
            .offset(y: rise)
            .frame(width: side * 1.2, height: side * 1.2)
    }
}
