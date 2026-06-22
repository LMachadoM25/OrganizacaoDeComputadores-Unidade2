#pragma once

#include <string>
#include <vector>

struct Packet {
    int id = -1;
    int source = -1;
    int destination = -1;
    int current_router = -1;

    int created_cycle = 0;
    int delivered_cycle = -1;

    std::vector<int> route_history;

    std::string toString() const;
};