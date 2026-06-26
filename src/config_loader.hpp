#pragma once

#include "traffic_event.hpp"

#include <string>
#include <vector>

// Mapeamento de um terminal para a porta lower de um roteador folha.
struct TerminalMap {
    int terminal = -1;
    int router   = -1;
    int port     = -1;
};

// Topologia carregada de arquivo (modelo K4,4 de 2 niveis).
struct TopologyConfig {
    int leaf_count = 0;               // roteadores folha (ids 0..leaf_count-1)
    int top_count  = 0;               // roteadores topo  (ids leaf_count..leaf_count+top_count-1)
    std::vector<TerminalMap> terminals;
};

// Carrega a topologia a partir de arquivo. Lanca std::runtime_error se invalida.
TopologyConfig loadTopology(const std::string& path);

// Carrega apenas o arquivo de trafego.
std::vector<TrafficEvent> loadTraffic(const std::string& path);
