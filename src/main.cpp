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

// ============================================================
// SpinSimulation — módulo SystemC
// Simula a rede SPIN por ciclos, injetando pacotes conforme
// o arquivo de tráfego e registrando o log.
// ============================================================
class SpinSimulation : public sc_core::sc_module {
public:
    SC_HAS_PROCESS(SpinSimulation);

    SpinSimulation(
        sc_core::sc_module_name name,
        std::vector<TrafficEvent> traffic_events,
        int max_cycles,
        std::string output_path
    )
        : sc_core::sc_module(name),
          traffic_events_(std::move(traffic_events)),
          max_cycles_(max_cycles),
          output_path_(std::move(output_path))
    {
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

        output << "=======================================================\n";
        output << " Simulacao SystemC - Rede em Chip SPIN\n";
        output << " Topologia: Arvore Gorda Quaternaria - 8 Roteadores RSPIN\n";
        output << " Chaveamento: Wormhole\n";
        output << " Roteamento: Adaptativo (subida) + Determinisico (descida)\n";
        output << " Controle de fluxo: Baseado em creditos\n";
        output << " Arbitragem: Round-Robin\n";
        output << "=======================================================\n\n";

        network_.printTopology(output);

        int packet_id = 0;

        for (int cycle = 0; cycle <= max_cycles_; ++cycle) {
            // Injeta pacotes agendados para este ciclo
            for (const TrafficEvent& ev : traffic_events_) {
                if (ev.cycle == cycle) {
                    Packet pkt;
                    pkt.id                   = packet_id++;
                    pkt.source_terminal      = ev.source;
                    pkt.destination_terminal = ev.destination;
                    pkt.created_cycle        = cycle;

                    network_.injectPacket(pkt, cycle, output);
                }
            }

            network_.step(cycle, output);

            wait(1, sc_core::SC_NS); // 1 ciclo de clock
        }

        network_.printFinalReport(output);

        std::cout << "Simulacao concluida. Log: " << output_path_ << "\n";
        sc_core::sc_stop();
    }

private:
    SpinNetwork network_;
    std::vector<TrafficEvent> traffic_events_;
    int max_cycles_;
    std::string output_path_;
};

// ============================================================
int sc_main(int argc, char* argv[]) {
    std::string traffic_path = "input/traffic_1.txt";
    std::string output_path  = "output/simulation_log.txt";
    int max_cycles           = 20;

    if (argc >= 2) traffic_path = argv[1];
    if (argc >= 3) output_path  = argv[2];
    if (argc >= 4) max_cycles   = std::stoi(argv[3]);

    try {
        auto traffic = loadTraffic(traffic_path);

        SpinSimulation sim(
            "SpinSimulation",
            std::move(traffic),
            max_cycles,
            output_path
        );

        sc_core::sc_start();
    } catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
