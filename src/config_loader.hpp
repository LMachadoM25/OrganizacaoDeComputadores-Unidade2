#pragma once

#include "network.hpp"
#include "traffic_event.hpp"

#include <string>
#include <vector>

Network loadTopology(const std::string& path);

std::vector<TrafficEvent> loadTraffic(const std::string& path);