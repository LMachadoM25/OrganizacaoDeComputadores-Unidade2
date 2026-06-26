#pragma once

// ============================================================
// Network - modelo simplificado inspirado na rede SPIN
// Topologia em arvore gorda quaternaria (K4,4) de 2 niveis.
// ============================================================
//
//   Nivel topo (raiz):  R4-R7   (lower_neighbors = folhas)
//   Nivel folha:        R0-R3   (lower_neighbors = terminais)
//   Cada folha liga suas 4 portas upper aos 4 roteadores de topo.
//   Terminais: T0-T3 (R0), T4-T7 (R1), T8-T11 (R2), T12-T15 (R3)
//
// Roteamento por menor caminho: sobe adaptativo / desce deterministico.
// Pacotes tratados como unidade atomica (sem flits wormhole reais).
// Controle de fluxo simplificado por capacidade de buffer.
// ============================================================

#include "config_loader.hpp"
#include "packet.hpp"
#include "router.hpp"

#include <iosfwd>
#include <string>
#include <vector>

class SpinNetwork {
public:
    explicit SpinNetwork(const TopologyConfig& config);

    int routerCount()   const;
    int terminalCount() const;

    // Injeta pacote a partir de terminal fonte
    void injectPacket(const Packet& packet, int cycle, std::ostream& out);

    // Executa um ciclo de simulação
    void step(int cycle, std::ostream& out);

    // True se nao ha pacotes em nenhum buffer (para parada antecipada)
    bool isEmpty() const;

    // Relatórios
    void printTopology(std::ostream& out)     const;
    void printFinalReport(std::ostream& out)  const;

private:
    void buildTopology(const TopologyConfig& config);

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
