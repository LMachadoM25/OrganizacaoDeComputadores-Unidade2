#pragma once

// ============================================================
// Roteador RSPIN - modelo simplificado inspirado na SPIN
// ============================================================
//
// Estrutura:
//   - 4 portas LOWER (D0-D3) e 4 portas UPPER (U0-U3)
//   - buffers de entrada por porta + buffers centrais QDN/QUP
//     usados como overflow quando a entrada enche
//
// Roteamento por menor caminho na arvore gorda:
//   - destino abaixo  -> desce (deterministico)
//   - caso contrario  -> sobe (adaptativo: porta com mais credito)
//
// Pacotes tratados como unidade atomica (sem flits wormhole reais).
// Controle de fluxo simplificado por capacidade de buffer.
// ============================================================

#include "packet.hpp"

#include <array>
#include <cstddef>
#include <queue>
#include <string>
#include <vector>

// Número de portas inferiores (D0-D3) e superiores (U0-U3) do RSPIN
static constexpr int SPIN_LOWER_PORTS = 4;
static constexpr int SPIN_UPPER_PORTS = 4;

// Capacidades de buffer conforme especificação SPIN
static constexpr std::size_t INPUT_BUFFER_CAPACITY  = 4;   // por porta de entrada
static constexpr std::size_t CENTRAL_BUFFER_CAPACITY = 18; // buffers centrais QUP e QDN

// Créditos iniciais por canal (igual à capacidade do buffer receptor)
static constexpr int INITIAL_CREDITS = static_cast<int>(INPUT_BUFFER_CAPACITY);

// Nível do roteador na árvore gorda (0 = raiz, aumenta descendo)
// Com 8 roteadores em 2 níveis: nível 0 = R4-R7 (topo), nível 1 = R0-R3 (folha)

class SpinRouter {
public:
    explicit SpinRouter(int id = 0, int tree_level = 0);

    int id()        const;
    int treeLevel() const;

    // --------------------------------------------------------
    // Injeção de pacote vindo de um terminal (porta lower)
    // --------------------------------------------------------
    bool injectFromTerminal(const Packet& packet, int port_index);

    // --------------------------------------------------------
    // Recebe pacote de outro roteador via porta upper ou lower
    // --------------------------------------------------------
    bool receiveFromUpper(int port_index, const Packet& packet);
    bool receiveFromLower(int port_index, const Packet& packet);

    // --------------------------------------------------------
    // Processamento de um ciclo:
    //   Tenta encaminhar todos os pacotes nos buffers de entrada
    //   para os buffers de saída dos vizinhos (via next_* buffers).
    //   Retorna lista de (roteador_vizinho, pacote) para entregar.
    // --------------------------------------------------------
    struct ForwardRequest {
        int  target_router;   // roteador de destino (-1 = entrega local ao terminal)
        int  target_port;     // porta no roteador alvo (-1 se entrega local)
        PortType via_port_type; // tipo da porta de saída usada
        Packet packet;
    };

    std::vector<ForwardRequest> process(int cycle, std::ostream& out);

    // Avança buffers: move next_ → current_
    void commitNextCycle();

    // --------------------------------------------------------
    // Controle de fluxo - créditos
    // --------------------------------------------------------
    // Retorna créditos disponíveis para enviar para uma porta
    int creditsForUpper(int port_index) const;
    int creditsForLower(int port_index) const;

    // Incrementa crédito quando receptor consome uma palavra
    void returnCreditUpper(int port_index);
    void returnCreditLower(int port_index);

    // --------------------------------------------------------
    // Conectividade: define vizinhos
    // --------------------------------------------------------
    void setUpperNeighbor(int port_index, int router_id);
    void setLowerNeighbor(int port_index, int router_id); // -1 = terminal

    int upperNeighbor(int port_index) const;
    int lowerNeighbor(int port_index) const;

    // --------------------------------------------------------
    // Status e diagnóstico
    // --------------------------------------------------------
    std::size_t totalOccupancy() const;
    std::string statusString()   const;

    // Copia dos pacotes ainda retidos em qualquer buffer (para relatorio).
    std::vector<Packet> pendingPackets() const;

private:
    // Tenta encaminhar um pacote; em caso de sucesso gera ForwardRequest.
    bool tryRoute(const Packet& pkt, std::vector<ForwardRequest>& reqs,
                  std::ostream& out, const std::string& src);

    // Roteamento adaptativo: escolhe porta upper com mais crédito (round-robin no desempate)
    int selectUpperPort(int destination_router) const;

    // Roteamento determinístico: escolhe porta lower com base no destino
    int selectLowerPort(int destination_router, int destination_terminal) const;

    // Verifica se o destino está acessível descendo (porta lower)
    bool destinationIsBelow(int destination_router) const;

private:
    int id_;
    int tree_level_; // 0 = nível raiz da árvore (roteadores de topo)

    // Vizinhos (router id, -1 se não conectado ou se terminal)
    std::array<int, SPIN_UPPER_PORTS> upper_neighbors_; // U0-U3: roteadores acima
    std::array<int, SPIN_LOWER_PORTS> lower_neighbors_; // D0-D3: roteadores/terminais abaixo

    // Buffers de entrada por porta (current cycle)
    std::array<std::queue<Packet>, SPIN_UPPER_PORTS> upper_in_current_; // entrada superior
    std::array<std::queue<Packet>, SPIN_LOWER_PORTS> lower_in_current_; // entrada inferior

    // Buffers de entrada por porta (next cycle — recebidos neste ciclo)
    std::array<std::queue<Packet>, SPIN_UPPER_PORTS> upper_in_next_;
    std::array<std::queue<Packet>, SPIN_LOWER_PORTS> lower_in_next_;

    // Buffers centrais: QDN (pacotes bloqueados descendo), QUP (bloqueados subindo)
    std::queue<Packet> central_qdn_current_;
    std::queue<Packet> central_qup_current_;
    std::queue<Packet> central_qdn_next_;
    std::queue<Packet> central_qup_next_;

    // Créditos de fluxo para cada porta vizinha
    std::array<int, SPIN_UPPER_PORTS> credits_upper_; // créditos para enviar upward
    std::array<int, SPIN_LOWER_PORTS> credits_lower_; // créditos para enviar downward

    // Round-robin: último port usado para arbitragem
    mutable int rr_upper_out_; // última porta upper usada como saída
    mutable int rr_lower_out_; // última porta lower usada como saída
};
