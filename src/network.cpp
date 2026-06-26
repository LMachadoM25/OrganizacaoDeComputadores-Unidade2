#include "network.hpp"

#include <algorithm>
#include <climits>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// ============================================================
// Construção da topologia SPIN com 8 roteadores
//
// Árvore gorda quaternária (fat-tree) de 2 níveis:
//
//         [R4]   [R5]   [R6]   [R7]     <- nível 1 (topo)
//          |  \ / | \ / | \ / |  |
//         [R0] [R1] [R2] [R3]           <- nível 2 (folha)
//          |    |    |    |
//        T0-3 T4-7 T8-11 T12-15         <- terminais
//
// Cada roteador folha (R0-R3) conecta suas 4 portas UPPER
// a todos os 4 roteadores de topo (R4-R7):
//   R0.U0 <-> R4.D0   R0.U1 <-> R5.D0
//   R0.U2 <-> R6.D0   R0.U3 <-> R7.D0  etc.
//
// Cada roteador folha conecta suas 4 portas LOWER
// a 4 terminais (-1 indica terminal, não roteador):
//   R0.D0 -> T0  R0.D1 -> T1  R0.D2 -> T2  R0.D3 -> T3
// ============================================================

SpinNetwork::SpinNetwork() {
    buildTopology();
}

void SpinNetwork::buildTopology() {
    // 8 roteadores: R0-R3 (nível folha, tree_level=1), R4-R7 (nível topo, tree_level=0)
    routers_.reserve(8);

    for (int i = 0; i < 4; ++i) {
        routers_.emplace_back(i, 1); // R0-R3: folha
    }
    for (int i = 4; i < 8; ++i) {
        routers_.emplace_back(i, 0); // R4-R7: topo
    }

    // --- Conexão folha <-> topo ---
    // Roteador folha Ri (i=0..3) usa portas U0-U3 para R4-R7
    // Roteador topo R(4+j) (j=0..3) usa porta Dj para o roteador folha Ri
    for (int leaf = 0; leaf < 4; ++leaf) {
        for (int top_port = 0; top_port < 4; ++top_port) {
            int top_router = 4 + top_port; // R4, R5, R6, R7

            // Folha: porta upper[top_port] aponta para roteador de topo
            routers_[leaf].setUpperNeighbor(top_port, top_router);

            // Topo: porta lower[leaf] aponta para roteador folha
            routers_[top_router].setLowerNeighbor(leaf, leaf);
        }
    }

    // --- Terminais nas portas lower dos roteadores folha ---
    // R0: T0-T3 (D0=T0, D1=T1, D2=T2, D3=T3)
    // R1: T4-T7
    // R2: T8-T11
    // R3: T12-T15
    terminal_to_router_.resize(16, -1);
    terminal_to_lower_port_.resize(16, -1);

    for (int leaf = 0; leaf < 4; ++leaf) {
        for (int port = 0; port < 4; ++port) {
            int terminal = leaf * 4 + port;
            terminal_to_router_[terminal]     = leaf;
            terminal_to_lower_port_[terminal] = port;

            // Porta lower do roteador folha = -1 (terminal, não outro roteador)
            routers_[leaf].setLowerNeighbor(port, -1);
        }
    }
}

// ============================================================
int SpinNetwork::routerCount() const {
    return static_cast<int>(routers_.size());
}

int SpinNetwork::terminalCount() const {
    int count = 0;
    for (int r : terminal_to_router_) if (r != -1) count++;
    return count;
}

bool SpinNetwork::validTerminal(int terminal_id) const {
    return terminal_id >= 0 &&
           terminal_id < static_cast<int>(terminal_to_router_.size()) &&
           terminal_to_router_[terminal_id] != -1;
}

int SpinNetwork::routerForTerminal(int terminal_id) const {
    return terminal_to_router_[terminal_id];
}

int SpinNetwork::lowerPortForTerminal(int /*router_id*/, int terminal_id) const {
    return terminal_to_lower_port_[terminal_id];
}

// ============================================================
// Injeção de pacote
// ============================================================
void SpinNetwork::injectPacket(const Packet& packet, int cycle, std::ostream& out) {
    if (!validTerminal(packet.source_terminal) ||
        !validTerminal(packet.destination_terminal)) {
        out << "Ciclo " << cycle << ": P" << packet.id
            << " descartado (terminal invalido)\n";
        dropped_packets_.push_back(packet);
        return;
    }

    Packet p = packet;
    p.source_router      = routerForTerminal(p.source_terminal);
    p.destination_router = routerForTerminal(p.destination_terminal);
    p.current_router     = p.source_router;
    p.created_cycle      = cycle;
    p.route_history.clear();
    // Bug 5 fix: não adiciona source_router aqui — step() registra cada hop corretamente
    p.entry_port = PortType::LOWER;

    // Bug 4 fix: passa a porta lower correta do terminal
    int port = lowerPortForTerminal(p.source_router, p.source_terminal);
    bool ok = routers_[p.source_router].injectFromTerminal(p, port);

    if (ok) {
        out << "Ciclo " << cycle << ": P" << p.id
            << " injetado T" << p.source_terminal
            << "->R" << p.source_router
            << " destino=T" << p.destination_terminal
            << "(R" << p.destination_router << ")\n";
    } else {
        out << "Ciclo " << cycle << ": P" << p.id
            << " descartado (buffer cheio em R" << p.source_router << ")\n";
        dropped_packets_.push_back(p);
    }
}

// ============================================================
// step() — executa um ciclo de simulação
// ============================================================
void SpinNetwork::step(int cycle, std::ostream& out) {
    out << "\n================ CICLO " << cycle << " ================\n";

    // Coleta todas as requisições de encaminhamento
    // (processamos todos os roteadores antes de entregar, evitando
    //  que um pacote seja processado duas vezes no mesmo ciclo)
    struct Delivery {
        int target_router;
        int target_port;
        PortType via_port_type;
        Packet packet;
        int from_router;
    };

    std::vector<Delivery> deliveries;

    for (int r = 0; r < routerCount(); ++r) {
        auto requests = routers_[r].process(cycle, out);

        for (auto& req : requests) {
            Delivery d;
            d.target_router   = req.target_router;
            d.target_port     = req.target_port;
            d.via_port_type   = req.via_port_type;
            d.packet          = req.packet;
            d.from_router     = r;
            deliveries.push_back(d);
        }
    }

    // Aplica as entregas
    for (auto& d : deliveries) {
        Packet& pkt = d.packet;
        // Bug 5 fix: registra o roteador que está encaminhando (sem duplicar fonte)
        pkt.route_history.push_back(d.from_router);

        // Entrega ao terminal
        if (d.target_router == -1) {
            pkt.delivered_cycle = cycle;
            pkt.current_router  = d.from_router;
            delivered_packets_.push_back(pkt);

            int latency = pkt.delivered_cycle - pkt.created_cycle;
            int hops    = static_cast<int>(pkt.route_history.size()) - 1;

            out << "  >>> P" << pkt.id
                << " ENTREGUE ao T" << pkt.destination_terminal
                << " em R" << d.from_router
                << " | latencia=" << latency << " ciclos"
                << " | hops=" << hops << "\n";
            continue;
        }

        // Encaminha para próximo roteador
        pkt.current_router = d.target_router;

        bool accepted = false;

        if (d.via_port_type == PortType::UPPER) {
            // Folha → Topo: o topo recebe pela porta lower cujo índice = ID do roteador folha.
            // Bug 2 fix: usar d.from_router como índice da porta lower do topo,
            // não d.target_port (que é a porta upper do roteador folha).
            accepted = routers_[d.target_router].receiveFromLower(d.from_router, pkt);

            // Bug 1 fix: crédito devolvido após commitNextCycle (ver abaixo).
            // Marcamos quais créditos devolver em uma lista separada.
        } else {
            // Topo → Folha: a folha recebe pela porta upper cujo índice = ID relativo no topo.
            // Bug 2 (descida): target_port aqui é a porta lower do topo, que é o ID da folha.
            // A porta upper da folha que conecta ao topo (d.from_router) é o offset do topo: from_router - 4.
            int upper_port_on_leaf = d.from_router - 4; // R4=0, R5=1, R6=2, R7=3
            accepted = routers_[d.target_router].receiveFromUpper(upper_port_on_leaf, pkt);
        }

        if (!accepted) {
            out << "  !!! P" << pkt.id
                << " descartado: buffer cheio em R" << d.target_router << "\n";
            dropped_packets_.push_back(pkt);
        }
    }

    // Bug 1 fix: avança buffers ANTES de devolver créditos.
    // O crédito só volta ao emissor depois que o receptor realmente consumiu
    // a posição (moved next_ → current_). Isso respeita a semântica de crédito SPIN.
    for (auto& r : routers_) {
        r.commitNextCycle();
    }

    // Bug 1 + 3 fix: devolve créditos após commit dos buffers
    for (auto& d : deliveries) {
        if (d.target_router == -1) continue; // entrega local: sem crédito a devolver

        if (d.via_port_type == PortType::UPPER) {
            // Folha → Topo: devolve crédito upper do roteador folha (emissor)
            for (int p = 0; p < SPIN_UPPER_PORTS; ++p) {
                if (routers_[d.from_router].upperNeighbor(p) == d.target_router) {
                    routers_[d.from_router].returnCreditUpper(p);
                    break;
                }
            }
        } else {
            // Bug 3 fix: Topo → Folha: devolve crédito lower do roteador TOPO (emissor),
            // buscando a porta lower cujo vizinho é o roteador folha destino.
            for (int p = 0; p < SPIN_LOWER_PORTS; ++p) {
                if (routers_[d.from_router].lowerNeighbor(p) == d.target_router) {
                    routers_[d.from_router].returnCreditLower(p);
                    break;
                }
            }
        }
    }

    // Estado dos buffers
    out << "\nBuffers (total de pacotes por roteador):\n";
    for (const auto& r : routers_) {
        out << "  " << r.statusString() << "\n";
    }

    total_cycles_simulated_ = cycle;
}

// ============================================================
// printTopology
// ============================================================
void SpinNetwork::printTopology(std::ostream& out) const {
    out << "========== TOPOLOGIA SPIN - ARVORE GORDA QUATERNARIA ==========\n";
    out << "Roteadores: " << routerCount() << " (RSPIN 8x8)\n";
    out << "Terminais:  " << terminalCount() << "\n\n";

    out << "Nivel 0 (topo):  R4, R5, R6, R7\n";
    out << "Nivel 1 (folha): R0, R1, R2, R3\n\n";

    out << "Conexoes (portas upper dos nos folha):\n";
    for (int leaf = 0; leaf < 4; ++leaf) {
        out << "  R" << leaf << " U0-U3 -> ";
        for (int p = 0; p < SPIN_UPPER_PORTS; ++p) {
            out << "R" << routers_[leaf].upperNeighbor(p);
            if (p < SPIN_UPPER_PORTS - 1) out << ", ";
        }
        out << "\n";
    }

    out << "\nConexoes (portas lower dos nos topo):\n";
    for (int top = 4; top < 8; ++top) {
        out << "  R" << top << " D0-D3 -> ";
        for (int p = 0; p < SPIN_LOWER_PORTS; ++p) {
            int n = routers_[top].lowerNeighbor(p);
            out << (n == -1 ? "-" : "R" + std::to_string(n));
            if (p < SPIN_LOWER_PORTS - 1) out << ", ";
        }
        out << "\n";
    }

    out << "\nTerminais conectados aos roteadores folha:\n";
    for (int t = 0; t < terminalCount(); ++t) {
        int r = terminal_to_router_[t];
        int p = terminal_to_lower_port_[t];
        out << "  T" << t << " -> R" << r << ".D" << p << "\n";
    }

    out << "\nBuffers por roteador:\n";
    out << "  Entrada por porta:   " << INPUT_BUFFER_CAPACITY   << " palavras\n";
    out << "  Buffer central QDN:  " << CENTRAL_BUFFER_CAPACITY << " palavras\n";
    out << "  Buffer central QUP:  " << CENTRAL_BUFFER_CAPACITY << " palavras\n";
    out << "  Creditos iniciais:   " << INITIAL_CREDITS         << " por canal\n\n";
}

// ============================================================
// printFinalReport
// ============================================================
void SpinNetwork::printFinalReport(std::ostream& out) const {
    out << "\n========== RELATORIO FINAL ==========\n";
    out << "Ciclos simulados:    " << total_cycles_simulated_ << "\n";
    out << "Pacotes entregues:   " << delivered_packets_.size() << "\n";
    out << "Pacotes descartados: " << dropped_packets_.size() << "\n";

    if (!delivered_packets_.empty()) {
        int    total_latency = 0;
        int    total_hops    = 0;
        int    max_latency   = 0;
        int    min_latency   = INT_MAX;

        out << "\nPacotes entregues:\n";

        for (const Packet& p : delivered_packets_) {
            int lat  = p.delivered_cycle - p.created_cycle;
            int hops = static_cast<int>(p.route_history.size()) - 1;

            total_latency += lat;
            total_hops    += hops;
            max_latency    = std::max(max_latency, lat);
            min_latency    = std::min(min_latency, lat);

            out << "  P" << p.id
                << " T" << p.source_terminal << "(R" << p.source_router << ")"
                << "->T" << p.destination_terminal << "(R" << p.destination_router << ")"
                << " criado=" << p.created_cycle
                << " entregue=" << p.delivered_cycle
                << " latencia=" << lat << "c"
                << " hops=" << hops
                << " rota=";

            for (std::size_t i = 0; i < p.route_history.size(); ++i) {
                out << "R" << p.route_history[i];
                if (i + 1 < p.route_history.size()) out << "->";
            }
            out << "\n";
        }

        int n = static_cast<int>(delivered_packets_.size());
        out << "\nEstatisticas:\n";
        out << "  Latencia media:  " << std::fixed
            << static_cast<double>(total_latency) / n << " ciclos\n";
        out << "  Latencia min:    " << min_latency << " ciclos\n";
        out << "  Latencia max:    " << max_latency << " ciclos\n";
        out << "  Media de saltos: "
            << static_cast<double>(total_hops) / n << " hops\n";
        out << "  Throughput:      "
            << static_cast<double>(n) / (total_cycles_simulated_ + 1)
            << " pacotes/ciclo\n";
    }

    if (!dropped_packets_.empty()) {
        out << "\nPacotes descartados:\n";
        for (const Packet& p : dropped_packets_) {
            out << "  P" << p.id
                << " T" << p.source_terminal
                << "->T" << p.destination_terminal << "\n";
        }
    }
}
