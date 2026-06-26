#pragma once

// ============================================================
// Pacote SPIN - Chaveamento Wormhole
// ============================================================
// Formato conforme especificação SPIN (LIP6/ASIM, Paris):
//
//   Flit 0 (cabeçalho):
//     bits [31:11] = protocolo
//     bit  [10]    = flag início de pacote (sempre 1 no header)
//     bits [9:0]   = destino (terminal)
//
//   Flits 1..N-1 (dados):
//     bits [31:0]  = dado
//     flag de fim de pacote no último flit
//
// Na simulação, representamos o pacote inteiro como struct.
// ============================================================

#include <string>
#include <vector>

// Tipo de porta de acordo com a árvore gorda
enum class PortType {
    LOWER,   // portas D0-D3: conectam ao nível inferior (terminais ou roteadores filhos)
    UPPER    // portas U0-U3: conectam ao nível superior (roteadores pai)
};

// Representa um flit de cabeçalho SPIN decodificado
struct SpinHeader {
    int  destination_terminal = -1;  // bits [9:0]
    bool start_of_packet      = true; // bit [10]
    int  protocol             = 0;   // bits [31:11]
};

struct Packet {
    int id                    = -1;

    // Endereçamento lógico (terminais)
    int source_terminal       = -1;
    int destination_terminal  = -1;

    // Roteadores correspondentes
    int source_router         = -1;
    int destination_router    = -1;

    // Estado atual no caminho
    int current_router        = -1;

    // Porta pela qual o pacote entrou no roteador atual
    // (LOWER = veio de baixo/terminal, UPPER = veio de cima/outro roteador)
    PortType entry_port       = PortType::LOWER;

    // Controle de ciclos
    int created_cycle         = 0;
    int delivered_cycle       = -1;

    // Histórico do caminho percorrido (para log)
    std::vector<int> route_history;

    // Payload simulado (número de flits de dado)
    int flit_count            = 1;

    std::string toString() const;
};
