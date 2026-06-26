#include <systemc>

#include "config_loader.hpp"
#include "network.hpp"
#include "packet.hpp"
#include "traffic_event.hpp"

#include <algorithm>
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
        const TopologyConfig& topology,
        std::vector<TrafficEvent> traffic_events,
        int max_cycles,
        std::string output_path
    )
        : sc_core::sc_module(name),
          network_(topology),
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
        output << " Modelo simplificado de rede em chip inspirado na SPIN\n";
        output << " Topologia: arvore gorda quaternaria (K4,4, 2 niveis) - 8 roteadores\n";
        output << " Roteamento: menor caminho (sobe adaptativo / desce deterministico)\n";
        output << " Pacotes: unidade atomica (sem flits wormhole reais)\n";
        output << " Controle de fluxo: simplificado por capacidade de buffer\n";
        output << "=======================================================\n\n";

        network_.printTopology(output);

        int last_event_cycle = 0;
        for (const TrafficEvent& ev : traffic_events_)
            last_event_cycle = std::max(last_event_cycle, ev.cycle);

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

            // Parada antecipada: todos os eventos injetados e rede vazia
            if (cycle >= last_event_cycle && network_.isEmpty()) {
                output << "\n(parada antecipada: rede vazia no ciclo " << cycle << ")\n";
                break;
            }
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
    // Uso: spin_network [topologia] [trafego] [saida] [ciclos]
    std::string topology_path = "input/topology_8.txt";
    std::string traffic_path  = "input/traffic_1.txt";
    std::string output_path   = "output/simulation_log.txt";
    int max_cycles            = 20;

    if (argc >= 2) topology_path = argv[1];
    if (argc >= 3) traffic_path  = argv[2];
    if (argc >= 4) output_path   = argv[3];
    if (argc >= 5) max_cycles    = std::stoi(argv[4]);

    try {
        auto topology = loadTopology(topology_path);
        auto traffic  = loadTraffic(traffic_path);

        SpinSimulation sim(
            "SpinSimulation",
            topology,
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
