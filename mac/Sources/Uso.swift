import SwiftUI

/// O que o Mac sabe e a placa não mostra.
///
/// Estas quatro seções saíram do `Panel.swift` inteiras, sem reestilizar nada. É
/// deliberado: a aba Board é a réplica da placa e tem de parecer com a placa, e
/// esta aqui é painel do Mac e tem de parecer com o Mac. Uniformizar as duas
/// perderia a única coisa que a réplica tem para dizer — que aquilo é a tela do
/// aparelho, e isto não é.
///
/// O que vive aqui é o que a placa não tem canal para mostrar: o uso calculado
/// das transcrições locais, o volume do dia, a lista de sessões com uma cara
/// cada, e o estado da própria ligação com o aparelho.

struct AbaUso: View {
    @ObservedObject var bridge: Bridge

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let d = bridge.data {
                if d.open_network == true { networkWarning }
                if let w = d.windows, w.ok { windowUsage(w) }
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

    /// Usage computed here, from the transcripts.
    ///
    /// It used to sit above the subscription limits, and the reason was that
    /// this number is always current while that one depends on a cache Claude
    /// Code sometimes lets age for days. The ordering argument survived the
    /// move: the limits are now on the Board tab, drawn as the board draws
    /// them, and this — which the board never had — got a tab of its own.
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
