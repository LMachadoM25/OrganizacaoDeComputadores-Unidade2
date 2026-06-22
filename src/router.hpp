#pragma once

#include "packet.hpp"

#include <cstddef>
#include <queue>

class Router {
public:
    explicit Router(int id = 0, std::size_t buffer_capacity = 8);

    int id() const;

    bool pushCurrent(const Packet& packet);
    bool pushNext(const Packet& packet);

    bool hasCurrentPacket() const;
    Packet popCurrent();

    void commitNextCycle();

    std::size_t currentOccupancy() const;
    std::size_t nextOccupancy() const;

private:
    int id_;
    std::size_t buffer_capacity_;

    std::queue<Packet> current_buffer_;
    std::queue<Packet> next_buffer_;
};