#pragma once

#include "packet.hpp"
#include "router.hpp"

#include <iosfwd>
#include <vector>

class Network {
public:
    explicit Network(int router_count = 8);

    int routerCount() const;
    int terminalCount() const;

    void addBidirectionalLink(int a, int b);
    void addTerminal(int terminal_id, int router_id);

    void injectPacket(const Packet& packet, int cycle, std::ostream& out);

    void step(int cycle, std::ostream& out);

    void printTopology(std::ostream& out) const;
    void printFinalReport(std::ostream& out) const;

private:
    bool validRouter(int id) const;
    bool validTerminal(int id) const;

    int routerForTerminal(int terminal_id) const;
    int findNextHop(int source_router, int destination_router) const;

private:
    std::vector<Router> routers_;
    std::vector<std::vector<int>> adjacency_;

    // terminal_to_router_[T] = R
    std::vector<int> terminal_to_router_;

    std::vector<Packet> delivered_packets_;
    std::vector<Packet> dropped_packets_;
};