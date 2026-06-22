#include <systemc>

#include "config_loader.hpp"
#include "network.hpp"
#include "packet.hpp"
#include "traffic_event.hpp"

#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class SpinSimulation : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(SpinSimulation);

    SpinSimulation(
        sc_core::sc_module_name name,
        Network network,
        std::vector<TrafficEvent> traffic_events,
        int max_cycles,
        std::string output_path
    )
        : sc_core::sc_module(name),
          network_(std::move(network)),
          traffic_events_(std::move(traffic_events)),
          max_cycles_(max_cycles),
          output_path_(std::move(output_path)) {
        SC_THREAD(run);
    }

private:
    void run() {
        std::ofstream output(output_path_);

        if (!output.is_open()) {
            std::cerr << "Erro ao abrir arquivo de saida: " << output_path_ << "\n";
            sc_core::sc_stop();
            return;
        }

        output << "Simulacao SystemC - Rede em Chip SPIN com 8 roteadores\n";
        output << "Modelo atual: arvore gorda quaternaria simplificada com terminais nas folhas\n\n";

        network_.printTopology(output);

        int packet_id = 0;

        for (int cycle = 0; cycle <= max_cycles_; ++cycle) {
            for (const TrafficEvent& event : traffic_events_) {
                if (event.cycle == cycle) {
                    Packet packet;
                    packet.id = packet_id++;
                    packet.source_terminal = event.source;
                    packet.destination_terminal = event.destination;
                    packet.created_cycle = cycle;

                    network_.injectPacket(packet, cycle, output);
                }
            }

            network_.step(cycle, output);

            wait(1, sc_core::SC_NS);
        }

        network_.printFinalReport(output);

        std::cout << "Simulacao finalizada. Log salvo em: "
                  << output_path_ << "\n";

        sc_core::sc_stop();
    }

private:
    Network network_;
    std::vector<TrafficEvent> traffic_events_;
    int max_cycles_;
    std::string output_path_;
};

int sc_main(int argc, char* argv[]) {
    std::string topology_path = "input/topology_8.txt";
    std::string traffic_path = "input/traffic_1.txt";
    std::string output_path = "output/simulation_log.txt";
    int max_cycles = 20;

    if (argc >= 2) {
        topology_path = argv[1];
    }

    if (argc >= 3) {
        traffic_path = argv[2];
    }

    if (argc >= 4) {
        output_path = argv[3];
    }

    if (argc >= 5) {
        max_cycles = std::stoi(argv[4]);
    }

    try {
        Network network = loadTopology(topology_path);
        std::vector<TrafficEvent> traffic_events = loadTraffic(traffic_path);

        SpinSimulation simulation(
            "SpinSimulation",
            std::move(network),
            std::move(traffic_events),
            max_cycles,
            output_path
        );

        sc_core::sc_start();
    } catch (const std::exception& error) {
        std::cerr << "Erro: " << error.what() << "\n";
        return 1;
    }

    return 0;
}