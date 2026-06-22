#include "config_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string removeComment(const std::string& line) {
    std::size_t position = line.find('#');

    if (position == std::string::npos) {
        return line;
    }

    return line.substr(0, position);
}

std::string trim(const std::string& text) {
    std::size_t start = 0;

    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    std::size_t end = text.size();

    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(start, end - start);
}

} // namespace

Network loadTopology(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Nao foi possivel abrir o arquivo de topologia: " + path);
    }

    int router_count = 0;
    std::vector<std::pair<int, int>> links;
    std::vector<std::pair<int, int>> terminals;

    enum class Section {
        None,
        Routers,
        Terminals,
        Links
    };

    Section section = Section::None;

    std::string line;

    while (std::getline(file, line)) {
        line = trim(removeComment(line));

        if (line.empty()) {
            continue;
        }

        if (line == "[routers]") {
            section = Section::Routers;
            continue;
        }

        if (line == "[terminals]") {
            section = Section::Terminals;
            continue;
        }

        if (line == "[links]") {
            section = Section::Links;
            continue;
        }

        std::istringstream iss(line);

        if (section == Section::Routers) {
            iss >> router_count;
        } else if (section == Section::Terminals) {
            int terminal_id = 0;
            int router_id = 0;

            if (iss >> terminal_id >> router_id) {
                terminals.emplace_back(terminal_id, router_id);
            }
        } else if (section == Section::Links) {
            int a = 0;
            int b = 0;

            if (iss >> a >> b) {
                links.emplace_back(a, b);
            }
        }
    }

    if (router_count <= 0) {
        throw std::runtime_error("Arquivo de topologia sem quantidade valida de roteadores");
    }

    Network network(router_count);

    for (const auto& terminal : terminals) {
        network.addTerminal(terminal.first, terminal.second);
    }

    for (const auto& link : links) {
        network.addBidirectionalLink(link.first, link.second);
    }

    return network;
}

std::vector<TrafficEvent> loadTraffic(const std::string& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Nao foi possivel abrir o arquivo de trafego: " + path);
    }

    std::vector<TrafficEvent> events;

    bool in_traffic_section = false;

    std::string line;

    while (std::getline(file, line)) {
        line = trim(removeComment(line));

        if (line.empty()) {
            continue;
        }

        if (line == "[traffic]") {
            in_traffic_section = true;
            continue;
        }

        if (!in_traffic_section) {
            continue;
        }

        std::istringstream iss(line);

        TrafficEvent event;

        if (iss >> event.cycle >> event.source >> event.destination) {
            events.push_back(event);
        }
    }

    return events;
}