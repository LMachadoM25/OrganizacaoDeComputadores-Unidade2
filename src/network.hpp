#pragma once

// ============================================================
// Network - Rede SPIN com topologia em árvore gorda quaternária
// ============================================================
//
// Topologia para 8 roteadores RSPIN:
//
//   Nível 1 (topo/raiz):   R4, R5, R6, R7  (upper_neighbors = nenhum / entre si)
//   Nível 2 (folha):       R0, R1, R2, R3  (lower_neighbors = terminais)
//
//   Cada roteador do nível folha conecta suas 4 portas upper
//   aos 4 roteadores do nível topo (fullmesh entre níveis).
//
//   Cada roteador de nível 2 tem 4 terminais conectados nas
//   portas lower (D0-D3).
//
//   Terminais: T0-T3 (via R0), T4-T7 (via R1),
//              T8-T11 (via R2), T12-T15 (via R3)
//
// Roteamento wormhole conforme regras SPIN:
//   - Entrada lower → sobe adaptativo via porta upper
//   - Entrada upper → desce determinístico via porta lower
//   - Buffers centrais QDN/QUP para pacotes bloqueados
//
// Controle de fluxo por créditos implementado em SpinRouter.
// ============================================================

#include "packet.hpp"
#include "router.hpp"

#include <iosfwd>
#include <string>
#include <vector>

class SpinNetwork {
public:
    SpinNetwork();

    int routerCount()   const;
    int terminalCount() const;

    // Injeta pacote a partir de terminal fonte
    void injectPacket(const Packet& packet, int cycle, std::ostream& out);

    // Executa um ciclo de simulação
    void step(int cycle, std::ostream& out);

    // Relatórios
    void printTopology(std::ostream& out)     const;
    void printFinalReport(std::ostream& out)  const;

private:
    void buildTopology();

    bool validTerminal(int terminal_id) const;
    int  routerForTerminal(int terminal_id) const;

    // Encontra porta lower do roteador folha que conecta ao terminal
    int lowerPortForTerminal(int router_id, int terminal_id) const;

private:
    std::vector<SpinRouter> routers_;

    // terminal_to_router_[T] = router_id
    std::vector<int> terminal_to_router_;

    // terminal_to_lower_port_[T] = porta D no roteador folha
    std::vector<int> terminal_to_lower_port_;

    // Estatísticas
    std::vector<Packet> delivered_packets_;
    std::vector<Packet> dropped_packets_;

    int total_cycles_simulated_ = 0;
};
