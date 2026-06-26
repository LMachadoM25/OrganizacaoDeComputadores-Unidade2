#include "router.hpp"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <stdexcept>

// ============================================================
SpinRouter::SpinRouter(int id, int tree_level)
    : id_(id), tree_level_(tree_level), rr_upper_out_(0), rr_lower_out_(0)
{
    upper_neighbors_.fill(-1);
    lower_neighbors_.fill(-1);
    credits_upper_.fill(INITIAL_CREDITS);
    credits_lower_.fill(INITIAL_CREDITS);
}

int SpinRouter::id()        const { return id_; }
int SpinRouter::treeLevel() const { return tree_level_; }

// ============================================================
// Conectividade
// ============================================================
void SpinRouter::setUpperNeighbor(int port_index, int router_id) {
    upper_neighbors_[port_index] = router_id;
}

void SpinRouter::setLowerNeighbor(int port_index, int router_id) {
    lower_neighbors_[port_index] = router_id;
}

int SpinRouter::upperNeighbor(int port_index) const {
    return upper_neighbors_[port_index];
}

int SpinRouter::lowerNeighbor(int port_index) const {
    return lower_neighbors_[port_index];
}

// ============================================================
// Créditos de fluxo
// ============================================================
int SpinRouter::creditsForUpper(int port_index) const {
    return credits_upper_[port_index];
}

int SpinRouter::creditsForLower(int port_index) const {
    return credits_lower_[port_index];
}

void SpinRouter::returnCreditUpper(int port_index) {
    if (credits_upper_[port_index] < INITIAL_CREDITS) {
        credits_upper_[port_index]++;
    }
}

void SpinRouter::returnCreditLower(int port_index) {
    if (credits_lower_[port_index] < INITIAL_CREDITS) {
        credits_lower_[port_index]++;
    }
}

// ============================================================
// Injeção de terminal (entra pela porta lower)
// ============================================================
bool SpinRouter::injectFromTerminal(const Packet& packet, int port_index) {
    if (lower_in_next_[port_index].size() < INPUT_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        lower_in_next_[port_index].push(p);
        return true;
    }
    // Overflow: pacote injetado precisa subir -> QUP
    if (central_qup_next_.size() < CENTRAL_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        central_qup_next_.push(p);
        return true;
    }
    return false;
}

// ============================================================
// Recebe de outro roteador
// ============================================================
bool SpinRouter::receiveFromUpper(int port_index, const Packet& packet) {
    if (upper_in_next_[port_index].size() < INPUT_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::UPPER;
        upper_in_next_[port_index].push(p);
        return true;
    }
    // Overflow para buffer central QDN (pacote vem de cima -> descera)
    if (central_qdn_next_.size() < CENTRAL_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::UPPER;
        central_qdn_next_.push(p);
        return true;
    }
    return false;
}

bool SpinRouter::receiveFromLower(int port_index, const Packet& packet) {
    if (lower_in_next_[port_index].size() < INPUT_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        lower_in_next_[port_index].push(p);
        return true;
    }
    // Overflow para buffer central QUP (pacote vem de baixo -> subira)
    if (central_qup_next_.size() < CENTRAL_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        central_qup_next_.push(p);
        return true;
    }
    return false;
}

// ============================================================
// Roteamento: seleciona porta de saída upper (adaptativo)
// Prefere a porta com mais créditos; round-robin como desempate.
// ============================================================
int SpinRouter::selectUpperPort(int /*destination_router*/) const {
    int best_port    = -1;
    int best_credits = -1;

    for (int i = 0; i < SPIN_UPPER_PORTS; ++i) {
        int port = (rr_upper_out_ + i) % SPIN_UPPER_PORTS;
        if (upper_neighbors_[port] == -1) continue;
        if (credits_upper_[port] > best_credits) {
            best_credits = credits_upper_[port];
            best_port    = port;
        }
    }

    if (best_port != -1) {
        rr_upper_out_ = (best_port + 1) % SPIN_UPPER_PORTS;
    }

    return best_port;
}

// ============================================================
// Roteamento: seleciona porta de saída lower (determinístico)
// ============================================================
int SpinRouter::selectLowerPort(int destination_router, int destination_terminal) const {
    (void)destination_terminal;

    // Nó folha: lower ports = terminais. Destino neste roteador = entrega local.
    if (destination_router == id_) {
        return -1; // entrega local ao terminal
    }

    // Nó topo: procura a porta lower que conecta ao roteador destino, com crédito.
    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) {
        int port = (rr_lower_out_ + i) % SPIN_LOWER_PORTS;
        if (lower_neighbors_[port] == destination_router && credits_lower_[port] > 0) {
            rr_lower_out_ = (port + 1) % SPIN_LOWER_PORTS;
            return port;
        }
    }

    // Porta existe mas sem crédito = bloqueado
    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) {
        if (lower_neighbors_[i] == destination_router) {
            return -2; // destino encontrado mas bloqueado por falta de crédito
        }
    }

    return -1;
}

// ============================================================
// Verifica se o destino está acessível descendo (filho)
// ============================================================
bool SpinRouter::destinationIsBelow(int destination_router) const {
    if (destination_router == id_) return true;
    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) {
        if (lower_neighbors_[i] == destination_router) return true;
    }
    return false;
}

// ============================================================
// tryRoute: decide o destino de um pacote (local / desce / sobe).
// Mesma logica para entradas e buffers centrais, evitando que
// pacotes fiquem presos por tratamento direcional incorreto.
// ============================================================
bool SpinRouter::tryRoute(const Packet& pkt, std::vector<ForwardRequest>& reqs,
                          std::ostream& out, const std::string& src) {
    // Entrega local
    if (pkt.destination_router == id_) {
        reqs.push_back({-1, -1, PortType::LOWER, pkt});
        out << "  [R" << id_ << "] " << src << " entrega P" << pkt.id
            << " ao terminal T" << pkt.destination_terminal << "\n";
        return true;
    }

    // Destino abaixo: desce determinístico
    if (destinationIsBelow(pkt.destination_router)) {
        int dn = selectLowerPort(pkt.destination_router, pkt.destination_terminal);
        if (dn >= 0 && credits_lower_[dn] > 0) {
            credits_lower_[dn]--;
            reqs.push_back({lower_neighbors_[dn], dn, PortType::LOWER, pkt});
            out << "  [R" << id_ << "] " << src << " desce P" << pkt.id
                << " -> R" << lower_neighbors_[dn] << " (D" << dn << ")\n";
            return true;
        }
        return false;
    }

    // Caso contrário: sobe adaptativo
    int up = selectUpperPort(pkt.destination_router);
    if (up != -1 && credits_upper_[up] > 0) {
        credits_upper_[up]--;
        reqs.push_back({upper_neighbors_[up], up, PortType::UPPER, pkt});
        out << "  [R" << id_ << "] " << src << " sobe P" << pkt.id
            << " -> R" << upper_neighbors_[up] << " (U" << up << ")\n";
        return true;
    }
    return false;
}

// ============================================================
// process() — processa um ciclo
// Encaminha a cabeça de cada buffer de entrada e dos buffers
// centrais. Pacotes bloqueados permanecem e retentam no proximo
// ciclo (nenhum pacote e descartado silenciosamente aqui).
// ============================================================
std::vector<SpinRouter::ForwardRequest> SpinRouter::process(int cycle, std::ostream& out) {
    std::vector<ForwardRequest> requests;
    (void)cycle;

    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) {
        if (lower_in_current_[i].empty()) continue;
        Packet pkt = lower_in_current_[i].front();
        if (tryRoute(pkt, requests, out, "DN" + std::to_string(i)))
            lower_in_current_[i].pop();
        else
            out << "  [R" << id_ << "] DN" << i << " P" << pkt.id << " bloqueado\n";
    }

    for (int i = 0; i < SPIN_UPPER_PORTS; ++i) {
        if (upper_in_current_[i].empty()) continue;
        Packet pkt = upper_in_current_[i].front();
        if (tryRoute(pkt, requests, out, "UP" + std::to_string(i)))
            upper_in_current_[i].pop();
        else
            out << "  [R" << id_ << "] UP" << i << " P" << pkt.id << " bloqueado\n";
    }

    if (!central_qup_current_.empty()) {
        Packet pkt = central_qup_current_.front();
        if (tryRoute(pkt, requests, out, "QUP")) central_qup_current_.pop();
        else out << "  [R" << id_ << "] QUP P" << pkt.id << " ainda bloqueado\n";
    }

    if (!central_qdn_current_.empty()) {
        Packet pkt = central_qdn_current_.front();
        if (tryRoute(pkt, requests, out, "QDN")) central_qdn_current_.pop();
        else out << "  [R" << id_ << "] QDN P" << pkt.id << " ainda bloqueado\n";
    }

    return requests;
}

// ============================================================
// commitNextCycle: move buffers next_ -> current_
// ============================================================
void SpinRouter::commitNextCycle() {
    for (int i = 0; i < SPIN_UPPER_PORTS; ++i) {
        while (!upper_in_next_[i].empty() &&
               upper_in_current_[i].size() < INPUT_BUFFER_CAPACITY) {
            upper_in_current_[i].push(upper_in_next_[i].front());
            upper_in_next_[i].pop();
        }
    }

    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) {
        while (!lower_in_next_[i].empty() &&
               lower_in_current_[i].size() < INPUT_BUFFER_CAPACITY) {
            lower_in_current_[i].push(lower_in_next_[i].front());
            lower_in_next_[i].pop();
        }
    }

    while (!central_qup_next_.empty() &&
           central_qup_current_.size() < CENTRAL_BUFFER_CAPACITY) {
        central_qup_current_.push(central_qup_next_.front());
        central_qup_next_.pop();
    }

    while (!central_qdn_next_.empty() &&
           central_qdn_current_.size() < CENTRAL_BUFFER_CAPACITY) {
        central_qdn_current_.push(central_qdn_next_.front());
        central_qdn_next_.pop();
    }
}

// ============================================================
// Status
// ============================================================
std::size_t SpinRouter::totalOccupancy() const {
    std::size_t total = 0;

    for (int i = 0; i < SPIN_UPPER_PORTS; ++i)
        total += upper_in_current_[i].size() + upper_in_next_[i].size();

    for (int i = 0; i < SPIN_LOWER_PORTS; ++i)
        total += lower_in_current_[i].size() + lower_in_next_[i].size();

    total += central_qup_current_.size() + central_qup_next_.size();
    total += central_qdn_current_.size() + central_qdn_next_.size();

    return total;
}

std::vector<Packet> SpinRouter::pendingPackets() const {
    std::vector<Packet> result;
    auto drain = [&](std::queue<Packet> q) {
        while (!q.empty()) { result.push_back(q.front()); q.pop(); }
    };
    for (int i = 0; i < SPIN_UPPER_PORTS; ++i) { drain(upper_in_current_[i]); drain(upper_in_next_[i]); }
    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) { drain(lower_in_current_[i]); drain(lower_in_next_[i]); }
    drain(central_qup_current_); drain(central_qup_next_);
    drain(central_qdn_current_); drain(central_qdn_next_);
    return result;
}

std::string SpinRouter::statusString() const {
    std::ostringstream oss;

    oss << "R" << id_ << " [nivel=" << tree_level_ << "]";
    oss << " DN(in):";
    for (int i = 0; i < SPIN_LOWER_PORTS; ++i)
        oss << lower_in_current_[i].size() << "/" << lower_in_next_[i].size() << " ";
    oss << " UP(in):";
    for (int i = 0; i < SPIN_UPPER_PORTS; ++i)
        oss << upper_in_current_[i].size() << "/" << upper_in_next_[i].size() << " ";
    oss << " QDN:" << central_qdn_current_.size();
    oss << " QUP:" << central_qup_current_.size();
    oss << " total=" << totalOccupancy();

    return oss.str();
}
