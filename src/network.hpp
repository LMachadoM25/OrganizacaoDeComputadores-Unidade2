#pragma once

#include "packet.hpp"
#include "router.hpp"

#include <iosfwd>
#include <vector>

class Network {
public:
    explicit Network(int router_count = 8);

    int routerCount() const;

    void addBidirectionalLink(int a, int b);

    void injectPacket(const Packet& packet, int cycle, std::ostream& out);

    void step(int cycle, std::ostream& out);

    void printTopology(std::ostream& out) const;
    void printFinalReport(std::ostream& out) const;

private:
    bool validRouter(int id) const;
    int findNextHop(int source, int destination) const;

private:
    std::vector<Router> routers_;
    std::vector<std::vector<int>> adjacency_;

    std::vector<Packet> delivered_packets_;
    std::vector<Packet> dropped_packets_;
};