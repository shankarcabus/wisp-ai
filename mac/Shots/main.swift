import SwiftUI
import AppKit

// Renders the app's screens to PNGs for the documentation.
//
// WHY NOT JUST TAKE A SCREENSHOT
// ------------------------------
// A real screenshot of the panel carries real project names, real usage and
// the board's address on the local network — into a public README, permanently.
// Here the data is synthetic, so nothing personal ships with the docs.
//
// It also makes the awkward states easy: photographing `error` or `waiting`
// live means provoking a failure at the exact moment the shutter is open.
//
// And it is repeatable — the images can be regenerated when the interface
// changes, instead of drifting until they show a version nobody runs.
//
//   ./mac/shots.sh
//
// WHAT THIS CANNOT DRAW, AND WHY IT IS NOT A BUG
// ----------------------------------------------
// The Settings tab. `ImageRenderer` cannot draw NSViews, and that tab is almost
// nothing but them — checkboxes, pickers, buttons — so it comes out as
// placeholder blocks. It is verified by building and running the app, not here.
// The two tabs below are pure SwiftUI and render exactly as they ship.

let panelJSON = """
{
  "uptime_s": 7412, "events": 1284,
  "board_ip": "192.168.0.31", "board_age_s": 1,
  "board_bat": 62, "board_bat_chg": false, "tasks_done": 37,
  "sessions": [
    {"st":"tool",    "dt":"Bash",         "pj":"wisp-ai",   "md":"opus-5",  "age":2},
    {"st":"asking",  "dt":"Auth method",  "pj":"storefront","md":"opus-5",  "age":14},
    {"st":"working", "dt":"thinking",     "pj":"api",       "md":"sonnet-5","age":5}
  ],
  "idle_age_s": 2, "rest_s": 300,
  "limits": [
    {"l":"5h window",  "p":41, "r":"3h", "s":"normal",  "a":true,  "e":58},
    {"l":"7 day",      "p":88, "r":"4d", "s":"warning", "a":false, "e":74},
    {"l":"opus 7 day", "p":22, "r":"4d", "s":"normal",  "a":false, "e":31}
  ],
  "limits_age_s": 48, "limits_source": "live", "peak": 88,
  "usage": {"requests": 1841, "input": 92130, "output": 664200, "cache_read": 18400000},
  "open_network": false,
  "windows": {
    "ok": true,
    "session": {"output": 606311, "reqs": 638,   "peak": 967881,  "pct": 63},
    "week":    {"output": 4448942,"reqs": 12806, "peak": 6451634, "pct": 69},
    "history_d": 29.3
  },
  "weather": {"t": 24, "c": "partly cloudy", "i": "cloudsun", "hi": 29, "lo": 18}
}
"""

// The same board, gone quiet: no sessions at all, so the mascot screen becomes
// rest mode. It is the only way to photograph the clock — waiting five real
// minutes with the shutter open is the alternative.
let restJSON = """
{
  "uptime_s": 21400, "events": 1284,
  "board_ip": "192.168.0.31", "board_age_s": 1,
  "board_bat": 62, "board_bat_chg": false, "tasks_done": 37,
  "sessions": [],
  "idle_age_s": 1840, "rest_s": 300,
  "limits": [
    {"l":"5h window",  "p":41, "r":"3h", "s":"normal",  "a":true,  "e":58},
    {"l":"7 day",      "p":88, "r":"4d", "s":"warning", "a":false, "e":74},
    {"l":"opus 7 day", "p":22, "r":"4d", "s":"normal",  "a":false, "e":31}
  ],
  "limits_age_s": 48, "limits_source": "live", "peak": 88,
  "usage": {"requests": 1841, "input": 92130, "output": 664200, "cache_read": 18400000},
  "open_network": false,
  "windows": {"ok": false, "session": null, "week": null, "history_d": null},
  "weather": {"t": 24, "c": "partly cloudy", "i": "cloudsun", "hi": 29, "lo": 18}
}
"""

@MainActor
func write<V: View>(_ view: V, _ name: String, scale: CGFloat = 2, opaque: Bool = false) {
    let r = ImageRenderer(content: view)
    r.scale = scale
    r.isOpaque = opaque
    guard let img = r.nsImage, let tiff = img.tiffRepresentation,
          let rep = NSBitmapImageRep(data: tiff),
          let png = rep.representation(using: .png, properties: [:]) else {
        print("failed: \(name)"); exit(1)
    }
    let url = URL(fileURLWithPath: "docs/\(name).png")
    try! png.write(to: url)
    print("  docs/\(name).png  \(Int(img.size.width))x\(Int(img.size.height))")
}

/// A tab, on the material background the menu bar gives the popover.
///
/// Rendered opaque, because a transparent PNG of a popover reads as a floating
/// rectangle on GitHub's white and its dark theme alike.
@MainActor
func aba<V: View>(_ conteudo: V) -> some View {
    VStack(alignment: .leading, spacing: 12) { conteudo }
        .padding(14)
        .frame(width: 320)
        .background(Color(nsColor: .windowBackgroundColor))
}

MainActor.assumeIsolated {
    // Render from the art the repository ships, not from ~/.wisp/mascots.
    // Otherwise these images depend on what happens to be installed on the
    // machine that generated them — and anybody regenerating them gets a
    // different README.
    Sprites.folder = URL(fileURLWithPath: "firmware")

    // E o arquivo publicado vai para o lixo, pelo mesmo motivo e com mais
    // urgência: o setter de Sprites.chosen PUBLICA em ~/.wisp/ui.json, para a
    // placa seguir a escolha do painel. Sem isto, rodar esta ferramenta de
    // documentação reescreve a configuração de quem rodou — e com um nome de
    // personagem, "assets", que só existe aqui dentro. Aconteceu uma vez.
    Ajustes.arquivo = URL(fileURLWithPath: NSTemporaryDirectory())
        .appendingPathComponent("wisp-shots-ui.json")

    Sprites.chosen = "assets"

    let bridge = Bridge.fixture(panelJSON)
    let quieto = Bridge.fixture(restJSON)

    // The Board tab: the board's own two screens, on the Mac. This is the
    // headline image, because it is the whole idea of the app in one picture.
    write(aba(AbaPlaca(bridge: bridge, lado: 320 - 28)), "panel", opaque: true)

    // The same tab with the board asleep, which is the only way to show the
    // clock and the weather.
    write(aba(AbaPlaca(bridge: quieto, lado: 320 - 28)), "panel-rest", opaque: true)

    // The Usage tab: what the Mac knows and the board has no channel to show.
    write(aba(AbaUso(bridge: bridge)), "panel-usage", opaque: true)

    // The desktop mascot, transparent, the way it actually sits on a desktop.
    write(FloatingContent(bridge: bridge), "floating")

    // The eight states in a row: the single most communicative image, because
    // the whole project is one idea — a face that tells you where Claude is.
    write(HStack(alignment: .top, spacing: 18) {
        ForEach(MascotState.allCases, id: \.self) { s in
            VStack(spacing: 6) {
                Mascot(state: s, side: 62)
                Text(s.label)
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(.white.opacity(0.78))
            }
        }
    }
    .padding(.horizontal, 22).padding(.vertical, 18)
    .background(Color(red: 0.09, green: 0.10, blue: 0.12)),
    "states", opaque: true)

    print("done")
}
