#include "router.hpp"

#include <stdexcept>

Router::Router(int id, std::size_t buffer_capacity)
    : id_(id), buffer_capacity_(buffer_capacity) {}

int Router::id() const {
    return id_;
}

bool Router::pushCurrent(const Packet& packet) {
    if (current_buffer_.size() >= buffer_capacity_) {
        return false;
    }

    current_buffer_.push(packet);
    return true;
}

bool Router::pushNext(const Packet& packet) {
    if (next_buffer_.size() >= buffer_capacity_) {
        return false;
    }

    next_buffer_.push(packet);
    return true;
}

bool Router::hasCurrentPacket() const {
    return !current_buffer_.empty();
}

Packet Router::popCurrent() {
    if (current_buffer_.empty()) {
        throw std::runtime_error("Tentativa de remover pacote de buffer vazio");
    }

    Packet packet = current_buffer_.front();
    current_buffer_.pop();

    return packet;
}

void Router::commitNextCycle() {
    while (!next_buffer_.empty() && current_buffer_.size() < buffer_capacity_) {
        current_buffer_.push(next_buffer_.front());
        next_buffer_.pop();
    }
}

std::size_t Router::currentOccupancy() const {
    return current_buffer_.size();
}

std::size_t Router::nextOccupancy() const {
    return next_buffer_.size();
}