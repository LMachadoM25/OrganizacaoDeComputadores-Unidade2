#include "network.hpp"

#include <algorithm>
#include <climits>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// ============================================================
// Construcao da topologia a partir do TopologyConfig (arquivo).
// Arvore gorda K4,4 de 2 niveis: folhas R0..(L-1), topos R L..(L+T-1).
// Cada folha liga suas portas upper a todos os roteadores de topo
// (full-mesh); terminais vem do mapeamento do arquivo.
// ============================================================

SpinNetwork::SpinNetwork(const TopologyConfig& config) {
    buildTopology(config);
}

void SpinNetwork::buildTopology(const TopologyConfig& config) {
    const int leaves = config.leaf_count;
    const int tops   = config.top_count;

    if (leaves > SPIN_UPPER_PORTS || tops > SPIN_UPPER_PORTS)
        throw std::runtime_error("Topologia excede o numero de portas do RSPIN");

    routers_.reserve(leaves + tops);
    for (int i = 0; i < leaves; ++i) routers_.emplace_back(i, 1);          // folha
    for (int i = 0; i < tops;   ++i) routers_.emplace_back(leaves + i, 0); // topo

    // Folha Ri usa porta upper j para o topo R(leaves+j);
    // topo R(leaves+j) usa porta lower i para a folha Ri.
    for (int leaf = 0; leaf < leaves; ++leaf) {
        for (int j = 0; j < tops; ++j) {
            int top_router = leaves + j;
            routers_[leaf].setUpperNeighbor(j, top_router);
            routers_[top_router].setLowerNeighbor(leaf, leaf);
        }
    }

    // Terminais (porta lower do roteador folha = -1, pois e terminal).
    int max_terminal = 0;
    for (const auto& m : config.terminals)
        max_terminal = std::max(max_terminal, m.terminal + 1);

    terminal_to_router_.assign(max_terminal, -1);
    terminal_to_lower_port_.assign(max_terminal, -1);

    for (const auto& m : config.terminals) {
        terminal_to_router_[m.terminal]     = m.router;
        terminal_to_lower_port_[m.terminal] = m.port;
        routers_[m.router].setLowerNeighbor(m.port, -1);
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

bool SpinNetwork::isEmpty() const {
    for (const auto& r : routers_)
        if (r.totalOccupancy() > 0) return false;
    return true;
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

    out << "\nBuffers por roteador (modelo simplificado):\n";
    out << "  Entrada por porta:        " << INPUT_BUFFER_CAPACITY   << " pacotes\n";
    out << "  Buffer central QDN/QUP:   " << CENTRAL_BUFFER_CAPACITY << " pacotes (overflow)\n";
    out << "  Controle de fluxo:        por capacidade de buffer\n\n";
}

// ============================================================
// printFinalReport
// ============================================================
void SpinNetwork::printFinalReport(std::ostream& out) const {
    // Pacotes ainda retidos em buffers de roteadores ao fim da simulacao.
    std::vector<Packet> pending;
    for (const auto& r : routers_) {
        auto p = r.pendingPackets();
        pending.insert(pending.end(), p.begin(), p.end());
    }

    out << "\n========== RELATORIO FINAL ==========\n";
    out << "Ciclos simulados:    " << total_cycles_simulated_ << "\n";
    out << "Pacotes entregues:   " << delivered_packets_.size() << "\n";
    out << "Pacotes descartados: " << dropped_packets_.size() << "\n";
    out << "Pacotes pendentes:   " << pending.size() << "\n";

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

    if (!pending.empty()) {
        out << "\nPacotes pendentes (retidos em buffers):\n";
        for (const Packet& p : pending) {
            out << "  P" << p.id
                << " T" << p.source_terminal
                << "->T" << p.destination_terminal
                << " (atual=R" << p.current_router << ")\n";
        }
    }
}
