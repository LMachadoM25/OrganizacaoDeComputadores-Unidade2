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

bool Network::validRouter(int id) const {
    return id >= 0 && id < routerCount();
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
    if (!validRouter(packet.source) || !validRouter(packet.destination)) {
        out << "Ciclo " << cycle << ": pacote P" << packet.id
            << " descartado por origem/destino invalido\n";

        dropped_packets_.push_back(packet);
        return;
    }

    Packet injected = packet;
    injected.current_router = packet.source;
    injected.created_cycle = cycle;
    injected.route_history.clear();
    injected.route_history.push_back(packet.source);

    bool inserted = routers_[packet.source].pushCurrent(injected);

    if (inserted) {
        out << "Ciclo " << cycle << ": pacote P" << injected.id
            << " injetado em R" << injected.source
            << " com destino R" << injected.destination << "\n";
    } else {
        out << "Ciclo " << cycle << ": pacote P" << injected.id
            << " descartado: buffer cheio em R" << injected.source << "\n";

        dropped_packets_.push_back(injected);
    }
}

int Network::findNextHop(int source, int destination) const {
    if (source == destination) {
        return source;
    }

    std::vector<int> previous(routerCount(), -1);
    std::queue<int> pending;

    previous[source] = source;
    pending.push(source);

    while (!pending.empty()) {
        int current = pending.front();
        pending.pop();

        for (int neighbor : adjacency_[current]) {
            if (previous[neighbor] != -1) {
                continue;
            }

            previous[neighbor] = current;

            if (neighbor == destination) {
                while (!pending.empty()) {
                    pending.pop();
                }

                break;
            }

            pending.push(neighbor);
        }
    }

    if (previous[destination] == -1) {
        return -1;
    }

    int node = destination;

    while (previous[node] != source) {
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

        if (packet.destination == router_id) {
            packet.delivered_cycle = cycle;
            delivered_packets_.push_back(packet);

            out << "R" << router_id << " entregou P" << packet.id
                << " | latencia=" << (packet.delivered_cycle - packet.created_cycle)
                << " ciclos"
                << " | " << packet.toString() << "\n";

            continue;
        }

        int next_hop = findNextHop(router_id, packet.destination);

        if (next_hop == -1) {
            out << "R" << router_id << " descartou P" << packet.id
                << ": nao ha rota ate R" << packet.destination << "\n";

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
                << " | destino=R" << packet.destination << "\n";
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

    out << "\nBuffers atuais:\n";

    for (const Router& router : routers_) {
        out << "R" << router.id()
            << ": " << router.currentOccupancy()
            << " pacote(s)\n";
    }
}

void Network::printTopology(std::ostream& out) const {
    out << "========== TOPOLOGIA ==========\n";

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
}

void Network::printFinalReport(std::ostream& out) const {
    out << "\n========== RELATORIO FINAL ==========\n";
    out << "Pacotes entregues: " << delivered_packets_.size() << "\n";
    out << "Pacotes descartados: " << dropped_packets_.size() << "\n";

    if (!delivered_packets_.empty()) {
        out << "\nPacotes entregues:\n";

        for (const Packet& packet : delivered_packets_) {
            out << "P" << packet.id
                << " origem=R" << packet.source
                << " destino=R" << packet.destination
                << " criado=" << packet.created_cycle
                << " entregue=" << packet.delivered_cycle
                << " latencia=" << (packet.delivered_cycle - packet.created_cycle)
                << " ciclos"
                << " rota=";

            for (std::size_t i = 0; i < packet.route_history.size(); ++i) {
                out << "R" << packet.route_history[i];

                if (i + 1 < packet.route_history.size()) {
                    out << "->";
                }
            }

            out << "\n";
        }
    }

    if (!dropped_packets_.empty()) {
        out << "\nPacotes descartados:\n";

        for (const Packet& packet : dropped_packets_) {
            out << "P" << packet.id
                << " origem=R" << packet.source
                << " destino=R" << packet.destination
                << "\n";
        }
    }
}