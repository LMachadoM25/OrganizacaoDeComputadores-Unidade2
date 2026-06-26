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
    // Usa a porta lower correspondente ao terminal (mapeamento fixo da topologia)
    // Bug 4 fix: não procura porta aleatória — usa o port_index correto
    if (lower_in_next_[port_index].size() < INPUT_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        lower_in_next_[port_index].push(p);
        return true;
    }
    // Tenta buffer central QDN como overflow
    if (central_qdn_next_.size() < CENTRAL_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        central_qdn_next_.push(p);
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
        // Devolve crédito ao emissor (simulado: o Network cuida disso)
        return true;
    }
    // Overflow para buffer central QUP
    if (central_qup_next_.size() < CENTRAL_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::UPPER;
        central_qup_next_.push(p);
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
    // Overflow para buffer central QDN
    if (central_qdn_next_.size() < CENTRAL_BUFFER_CAPACITY) {
        Packet p = packet;
        p.entry_port = PortType::LOWER;
        central_qdn_next_.push(p);
        return true;
    }
    return false;
}

// ============================================================
// Roteamento: seleciona porta de saída upper (adaptativo)
// Escolhe a porta upper com menor ocupação no roteador vizinho.
// Round-robin como desempate (conforme spec SPIN).
// ============================================================
int SpinRouter::selectUpperPort(int /*destination_router*/) const {
    // Adaptativo: prefere a porta upper com mais créditos disponíveis
    // (proxy para menor ocupação no vizinho)
    int best_port   = -1;
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

    // Nó folha (tree_level_==1): lower ports = terminais (-1)
    // O destino é este roteador → entrega local
    if (destination_router == id_) {
        return -1; // entrega local ao terminal
    }

    // Nó topo (tree_level_==0): lower ports = roteadores folha
    // Procura a porta que conecta ao roteador destino, com crédito
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
// process() — processa um ciclo
// Aplica regras de roteamento SPIN:
//   - Entrada lower → saída upper (adaptativo) OU saída lower (determinístico)
//   - Entrada upper → saída lower (determinístico apenas)
//   - Buffers centrais têm prioridade sobre entradas quando bloqueados
// ============================================================
std::vector<SpinRouter::ForwardRequest> SpinRouter::process(int cycle, std::ostream& out) {
    std::vector<ForwardRequest> requests;

    // --- Processa entradas das portas LOWER (DN units) ---
    // Regra SPIN:
    //   - Se destino está abaixo (vizinho lower): desce DETERMINÍSTICO
    //   - Caso contrário: sobe ADAPTATIVO pelas portas upper
    //   - Se subida bloqueada: buffer central QUP
    for (int i = 0; i < SPIN_LOWER_PORTS; ++i) {
        if (lower_in_current_[i].empty()) continue;

        Packet pkt = lower_in_current_[i].front();

        // Entrega local (destino é este roteador)
        if (pkt.destination_router == id_) {
            lower_in_current_[i].pop();
            ForwardRequest req;
            req.target_router  = -1;
            req.target_port    = -1;
            req.via_port_type  = PortType::LOWER;
            req.packet         = pkt;
            requests.push_back(req);
            out << "  [R" << id_ << "] DN" << i
                << " entrega P" << pkt.id
                << " ao terminal T" << pkt.destination_terminal << "\n";
            continue;
        }

        // Verifica se destino está diretamente abaixo (vizinho lower)
        bool dest_is_below = destinationIsBelow(pkt.destination_router);

        if (dest_is_below) {
            // Desce deterministicamente
            int dn_port = selectLowerPort(pkt.destination_router, pkt.destination_terminal);
            if (dn_port >= 0 && credits_lower_[dn_port] > 0) {
                lower_in_current_[i].pop();
                credits_lower_[dn_port]--;
                ForwardRequest req;
                req.target_router  = lower_neighbors_[dn_port];
                req.target_port    = dn_port;
                req.via_port_type  = PortType::LOWER;
                req.packet         = pkt;
                requests.push_back(req);
                out << "  [R" << id_ << "] DN" << i
                    << " desce P" << pkt.id
                    << " -> R" << req.target_router
                    << " (D" << dn_port << ", credito=" << credits_lower_[dn_port] << ")\n";
            } else {
                // Bloqueado → QDN
                if (central_qdn_next_.size() < CENTRAL_BUFFER_CAPACITY) {
                    lower_in_current_[i].pop();
                    central_qdn_next_.push(pkt);
                    out << "  [R" << id_ << "] DN" << i
                        << " P" << pkt.id << " bloqueado (desce) -> QDN\n";
                } else {
                    out << "  [R" << id_ << "] DN" << i
                        << " P" << pkt.id << " bloqueado (QDN cheio)\n";
                }
            }
        } else {
            // Sobe adaptativo
            int up_port = selectUpperPort(pkt.destination_router);

            if (up_port != -1 && credits_upper_[up_port] > 0) {
                lower_in_current_[i].pop();
                credits_upper_[up_port]--;

                ForwardRequest req;
                req.target_router  = upper_neighbors_[up_port];
                req.target_port    = up_port;
                req.via_port_type  = PortType::UPPER;
                req.packet         = pkt;
                requests.push_back(req);

                out << "  [R" << id_ << "] DN" << i
                    << " sobe P" << pkt.id
                    << " -> R" << req.target_router
                    << " (U" << up_port << ", credito=" << credits_upper_[up_port] << ")\n";
            } else {
                // Sobe bloqueado: move para buffer central QUP
                if (central_qup_next_.size() < CENTRAL_BUFFER_CAPACITY) {
                    lower_in_current_[i].pop();
                    central_qup_next_.push(pkt);
                    out << "  [R" << id_ << "] DN" << i
                        << " P" << pkt.id << " bloqueado (sobe) -> QUP\n";
                } else {
                    out << "  [R" << id_ << "] DN" << i
                        << " P" << pkt.id << " bloqueado (QUP cheio)\n";
                }
            }
        }
    }

    // --- Processa buffer central QUP ---
    if (!central_qup_current_.empty()) {
        Packet pkt = central_qup_current_.front();
        bool dest_is_below = destinationIsBelow(pkt.destination_router);

        if (dest_is_below) {
            int dn_port = selectLowerPort(pkt.destination_router, pkt.destination_terminal);
            if (dn_port >= 0 && credits_lower_[dn_port] > 0) {
                central_qup_current_.pop();
                credits_lower_[dn_port]--;
                ForwardRequest req;
                req.target_router = lower_neighbors_[dn_port];
                req.target_port   = dn_port;
                req.via_port_type = PortType::LOWER;
                req.packet        = pkt;
                requests.push_back(req);
                out << "  [R" << id_ << "] QUP desce P" << pkt.id
                    << " -> R" << req.target_router << " (D" << dn_port << ")\n";
            } else {
                out << "  [R" << id_ << "] QUP P" << pkt.id << " ainda bloqueado\n";
            }
        } else {
            int up_port = selectUpperPort(pkt.destination_router);
            if (up_port != -1 && credits_upper_[up_port] > 0) {
                central_qup_current_.pop();
                credits_upper_[up_port]--;
                ForwardRequest req;
                req.target_router  = upper_neighbors_[up_port];
                req.target_port    = up_port;
                req.via_port_type  = PortType::UPPER;
                req.packet         = pkt;
                requests.push_back(req);
                out << "  [R" << id_ << "] QUP sobe P" << pkt.id
                    << " -> R" << req.target_router << " (U" << up_port << ")\n";
            } else {
                out << "  [R" << id_ << "] QUP P" << pkt.id << " ainda bloqueado\n";
            }
        }
    }

    // --- Processa entradas das portas UPPER (UP units) ---
    // Regra: SOMENTE pode descer (lower, determinístico)
    for (int i = 0; i < SPIN_UPPER_PORTS; ++i) {
        if (upper_in_current_[i].empty()) continue;

        Packet pkt = upper_in_current_[i].front();

        // Se destino está neste roteador: entrega local
        if (pkt.destination_router == id_) {
            upper_in_current_[i].pop();
            ForwardRequest req;
            req.target_router  = -1;
            req.target_port    = -1;
            req.via_port_type  = PortType::LOWER;
            req.packet         = pkt;
            requests.push_back(req);
            out << "  [R" << id_ << "] UP" << i
                << " entrega P" << pkt.id
                << " ao terminal T" << pkt.destination_terminal << "\n";
            continue;
        }

        // Desce (determinístico)
        int dn_port = selectLowerPort(pkt.destination_router, pkt.destination_terminal);

        if (dn_port >= 0 && credits_lower_[dn_port] > 0) {
            upper_in_current_[i].pop();
            credits_lower_[dn_port]--;

            ForwardRequest req;
            req.target_router  = lower_neighbors_[dn_port];
            req.target_port    = dn_port;
            req.via_port_type  = PortType::LOWER;
            req.packet         = pkt;
            requests.push_back(req);

            out << "  [R" << id_ << "] UP" << i
                << " desce P" << pkt.id
                << " -> R" << req.target_router
                << " (D" << dn_port << ", credito=" << credits_lower_[dn_port] << ")\n";
        } else {
            // Bloqueado: vai para buffer central QDN
            if (central_qdn_next_.size() < CENTRAL_BUFFER_CAPACITY) {
                upper_in_current_[i].pop();
                central_qdn_next_.push(pkt);
                out << "  [R" << id_ << "] UP" << i
                    << " P" << pkt.id << " bloqueado -> QDN\n";
            } else {
                out << "  [R" << id_ << "] UP" << i
                    << " P" << pkt.id << " bloqueado (QDN cheio)\n";
            }
        }
    }

    // --- Processa buffer central QDN (pacotes bloqueados descendo) ---
    if (!central_qdn_current_.empty()) {
        Packet pkt = central_qdn_current_.front();
        int dn_port = selectLowerPort(pkt.destination_router, pkt.destination_terminal);

        if (dn_port >= 0 && credits_lower_[dn_port] > 0) {
            central_qdn_current_.pop();
            credits_lower_[dn_port]--;

            ForwardRequest req;
            req.target_router  = lower_neighbors_[dn_port];
            req.target_port    = dn_port;
            req.via_port_type  = PortType::LOWER;
            req.packet         = pkt;
            requests.push_back(req);

            out << "  [R" << id_ << "] QDN desce P" << pkt.id
                << " -> R" << req.target_router
                << " (D" << dn_port << ")\n";
        } else {
            out << "  [R" << id_ << "] QDN P" << pkt.id << " ainda bloqueado\n";
        }
    }

    (void)cycle;
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
