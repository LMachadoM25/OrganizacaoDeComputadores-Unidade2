#pragma once

#include "traffic_event.hpp"

#include <string>
#include <vector>

// Carrega apenas o arquivo de tráfego.
// A topologia SPIN é sempre a árvore gorda de 8 roteadores (hard-coded).
std::vector<TrafficEvent> loadTraffic(const std::string& path);
