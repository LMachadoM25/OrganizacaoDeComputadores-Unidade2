#include "config_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string removeComment(const std::string& line) {
    auto pos = line.find('#');
    return (pos == std::string::npos) ? line : line.substr(0, pos);
}

std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1])) --end;
    return s.substr(start, end - start);
}

} // namespace

TopologyConfig loadTopology(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Nao foi possivel abrir arquivo de topologia: " + path);

    TopologyConfig cfg;
    std::string section;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(removeComment(line));
        if (line.empty()) continue;

        if (line == "[routers]")   { section = "routers";   continue; }
        if (line == "[terminals]") { section = "terminals"; continue; }

        std::istringstream iss(line);
        if (section == "routers") {
            std::string type; int id;
            if (iss >> type >> id) {
                if (type == "leaf") cfg.leaf_count++;
                else if (type == "top") cfg.top_count++;
            }
        } else if (section == "terminals") {
            TerminalMap m;
            if (iss >> m.terminal >> m.router >> m.port)
                cfg.terminals.push_back(m);
        }
    }

    if (cfg.leaf_count <= 0 || cfg.top_count <= 0 || cfg.terminals.empty())
        throw std::runtime_error("Topologia invalida ou incompleta: " + path);

    return cfg;
}

std::vector<TrafficEvent> loadTraffic(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Nao foi possivel abrir arquivo de trafego: " + path);

    std::vector<TrafficEvent> events;
    bool in_traffic = false;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(removeComment(line));
        if (line.empty()) continue;

        if (line == "[traffic]") { in_traffic = true; continue; }
        if (!in_traffic) continue;

        std::istringstream iss(line);
        TrafficEvent ev;
        if (iss >> ev.cycle >> ev.source >> ev.destination)
            events.push_back(ev);
    }

    return events;
}
