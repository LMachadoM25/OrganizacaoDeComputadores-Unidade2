#include "packet.hpp"

#include <sstream>

std::string Packet::toString() const {
    std::ostringstream oss;

    oss << "P" << id
        << " T" << source_terminal << "(R" << source_router << ")"
        << "->T" << destination_terminal << "(R" << destination_router << ")"
        << " atual=R" << current_router;

    if (!route_history.empty()) {
        oss << " rota=";
        for (std::size_t i = 0; i < route_history.size(); ++i) {
            oss << "R" << route_history[i];
            if (i + 1 < route_history.size()) oss << "->";
        }
    }

    return oss.str();
}
