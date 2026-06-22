#include "network.hpp"

#include <algorithm>
#include <iostream>
#include <queue>
#include <stdexcept>

Network::Network(int router_count) {
    if (router_count <= 0) {
        throw std::runtime_error("Quantidade de roteadores invalida");
    }

    routers_.reserve(router_count);
    adjacency_.resize(router_count);

    for (int i = 0; i < router_count; ++i) {
        routers_.emplace_back(i);
    }
}

int Network::routerCount() const {
    return static_cast<int>(routers_.size());
}

int Network::terminalCount() const {
    int count = 0;

    for (int router_id : terminal_to_router_) {
        if (router_id != -1) {
            count++;
        }
    }

    return count;
}

bool Network::validRouter(int id) const {
    return id >= 0 && id < routerCount();
}

bool Network::validTerminal(int id) const {
    return id >= 0 &&
           id < static_cast<int>(terminal_to_router_.size()) &&
           terminal_to_router_[id] != -1 &&
           validRouter(terminal_to_router_[id]);
}

int Network::routerForTerminal(int terminal_id) const {
    if (!validTerminal(terminal_id)) {
        throw std::runtime_error("Terminal invalido: T" + std::to_string(terminal_id));
    }

    return terminal_to_router_[terminal_id];
}

void Network::addTerminal(int terminal_id, int router_id) {
    if (terminal_id < 0) {
        throw std::runtime_error("ID de terminal invalido");
    }

    if (!validRouter(router_id)) {
        throw std::runtime_error("Terminal conectado a roteador invalido");
    }

    if (terminal_id >= static_cast<int>(terminal_to_router_.size())) {
        terminal_to_router_.resize(terminal_id + 1, -1);
    }

    terminal_to_router_[terminal_id] = router_id;
}

void Network::addBidirectionalLink(int a, int b) {
    if (!validRouter(a) || !validRouter(b)) {
        throw std::runtime_error("Link possui roteador invalido");
    }

    adjacency_[a].push_back(b);
    adjacency_[b].push_back(a);

    std::sort(adjacency_[a].begin(), adjacency_[a].end());
    std::sort(adjacency_[b].begin(), adjacency_[b].end());
}

void Network::injectPacket(const Packet& packet, int cycle, std::ostream& out) {
    if (!validTerminal(packet.source_terminal) ||
        !validTerminal(packet.destination_terminal)) {
        out << "Ciclo " << cycle << ": pacote P" << packet.id
            << " descartado por terminal de origem/destino invalido\n";

        dropped_packets_.push_back(packet);
        return;
    }

    Packet injected = packet;

    injected.source_router = routerForTerminal(packet.source_terminal);
    injected.destination_router = routerForTerminal(packet.destination_terminal);
    injected.current_router = injected.source_router;
    injected.created_cycle = cycle;

    injected.route_history.clear();
    injected.route_history.push_back(injected.source_router);

    bool inserted = routers_[injected.source_router].pushCurrent(injected);

    if (inserted) {
        out << "Ciclo " << cycle << ": pacote P" << injected.id
            << " injetado em T" << injected.source_terminal
            << " via R" << injected.source_router
            << " com destino T" << injected.destination_terminal
            << " via R" << injected.destination_router
            << "\n";
    } else {
        out << "Ciclo " << cycle << ": pacote P" << injected.id
            << " descartado: buffer cheio em R" << injected.source_router << "\n";

        dropped_packets_.push_back(injected);
    }
}

int Network::findNextHop(int source_router, int destination_router) const {
    if (source_router == destination_router) {
        return source_router;
    }

    std::vector<int> previous(routerCount(), -1);
    std::queue<int> pending;

    previous[source_router] = source_router;
    pending.push(source_router);

    while (!pending.empty()) {
        int current = pending.front();
        pending.pop();

        for (int neighbor : adjacency_[current]) {
            if (previous[neighbor] != -1) {
                continue;
            }

            previous[neighbor] = current;

            if (neighbor == destination_router) {
                while (!pending.empty()) {
                    pending.pop();
                }

                break;
            }

            pending.push(neighbor);
        }
    }

    if (previous[destination_router] == -1) {
        return -1;
    }

    int node = destination_router;

    while (previous[node] != source_router) {
        node = previous[node];
    }

    return node;
}

void Network::step(int cycle, std::ostream& out) {
    out << "\n================ CICLO " << cycle << " ================\n";

    for (int router_id = 0; router_id < routerCount(); ++router_id) {
        Router& router = routers_[router_id];

        if (!router.hasCurrentPacket()) {
            continue;
        }

        Packet packet = router.popCurrent();

        if (packet.destination_router == router_id) {
            packet.current_router = router_id;
            packet.delivered_cycle = cycle;
            delivered_packets_.push_back(packet);

            out << "R" << router_id << " entregou P" << packet.id
                << " ao terminal T" << packet.destination_terminal
                << " | latencia=" << (packet.delivered_cycle - packet.created_cycle)
                << " ciclos"
                << " | " << packet.toString() << "\n";

            continue;
        }

        int next_hop = findNextHop(router_id, packet.destination_router);

        if (next_hop == -1) {
            out << "R" << router_id << " descartou P" << packet.id
                << ": nao ha rota ate R" << packet.destination_router << "\n";

            dropped_packets_.push_back(packet);
            continue;
        }

        Packet forwarded = packet;
        forwarded.current_router = next_hop;
        forwarded.route_history.push_back(next_hop);

        bool forwarded_ok = routers_[next_hop].pushNext(forwarded);

        if (forwarded_ok) {
            out << "R" << router_id << " encaminhou P" << packet.id
                << " para R" << next_hop
                << " | destino=T" << packet.destination_terminal
                << "(R" << packet.destination_router << ")"
                << "\n";
        } else {
            Packet blocked = packet;
            blocked.current_router = router_id;

            bool requeued = routers_[router_id].pushNext(blocked);

            out << "R" << router_id << " bloqueou P" << packet.id
                << ": buffer cheio em R" << next_hop;

            if (requeued) {
                out << " | pacote permanece em R" << router_id;
            } else {
                out << " | pacote descartado por buffer local cheio";
                dropped_packets_.push_back(packet);
            }

            out << "\n";
        }
    }

    for (Router& router : routers_) {
        router.commitNextCycle();
    }

    out << "\nBuffers atuais dos roteadores:\n";

    for (const Router& router : routers_) {
        out << "R" << router.id()
            << ": " << router.currentOccupancy()
            << " pacote(s)\n";
    }
}

void Network::printTopology(std::ostream& out) const {
    out << "========== TOPOLOGIA SPIN SIMPLIFICADA ==========\n";
    out << "Roteadores: " << routerCount() << "\n";
    out << "Terminais: " << terminalCount() << "\n\n";

    out << "Links entre roteadores:\n";

    for (int i = 0; i < routerCount(); ++i) {
        out << "R" << i << " -> ";

        for (std::size_t j = 0; j < adjacency_[i].size(); ++j) {
            out << "R" << adjacency_[i][j];

            if (j + 1 < adjacency_[i].size()) {
                out << ", ";
            }
        }

        out << "\n";
    }

    out << "\nTerminais conectados aos roteadores:\n";

    for (int terminal_id = 0;
         terminal_id < static_cast<int>(terminal_to_router_.size());
         ++terminal_id) {
        if (terminal_to_router_[terminal_id] == -1) {
            continue;
        }

        out << "T" << terminal_id
            << " -> R" << terminal_to_router_[terminal_id]
            << "\n";
    }
}

void Network::printFinalReport(std::ostream& out) const {
    out << "\n========== RELATORIO FINAL ==========\n";
    out << "Pacotes entregues: " << delivered_packets_.size() << "\n";
    out << "Pacotes descartados: " << dropped_packets_.size() << "\n";

    int total_latency = 0;
    int total_hops = 0;

    if (!delivered_packets_.empty()) {
        out << "\nPacotes entregues:\n";

        for (const Packet& packet : delivered_packets_) {
            int latency = packet.delivered_cycle - packet.created_cycle;
            int hops = static_cast<int>(packet.route_history.size()) - 1;

            total_latency += latency;
            total_hops += hops;

            out << "P" << packet.id
                << " origem=T" << packet.source_terminal
                << "(R" << packet.source_router << ")"
                << " destino=T" << packet.destination_terminal
                << "(R" << packet.destination_router << ")"
                << " criado=" << packet.created_cycle
                << " entregue=" << packet.delivered_cycle
                << " latencia=" << latency
                << " ciclos"
                << " hops=" << hops
                << " rota=";

            for (std::size_t i = 0; i < packet.route_history.size(); ++i) {
                out << "R" << packet.route_history[i];

                if (i + 1 < packet.route_history.size()) {
                    out << "->";
                }
            }

            out << "\n";
        }

        double average_latency =
            static_cast<double>(total_latency) /
            static_cast<double>(delivered_packets_.size());

        double average_hops =
            static_cast<double>(total_hops) /
            static_cast<double>(delivered_packets_.size());

        out << "\nEstatisticas:\n";
        out << "Latencia media: " << average_latency << " ciclos\n";
        out << "Media de saltos: " << average_hops << " hops\n";
    }

    if (!dropped_packets_.empty()) {
        out << "\nPacotes descartados:\n";

        for (const Packet& packet : dropped_packets_) {
            out << "P" << packet.id
                << " origem=T" << packet.source_terminal
                << " destino=T" << packet.destination_terminal
                << "\n";
        }
    }
}