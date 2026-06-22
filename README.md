# Rede em Chip SPIN com 8 Roteadores - SystemC

Este projeto implementa uma base inicial de simulação de uma rede em chip inspirada na arquitetura SPIN, usando SystemC.

## Objetivo

Simular pacotes trafegando entre 8 roteadores, registrando:

- caminho percorrido;
- entrega dos pacotes;
- latência em ciclos;
- estado dos buffers por ciclo.

## Estrutura

```txt
.
├── CMakeLists.txt
├── README.md
├── input/
│   ├── topology_8.txt
│   └── traffic_1.txt
├── output/
│   └── simulation_log.txt
├── src/
│   ├── main.cpp
│   ├── packet.hpp
│   ├── packet.cpp
│   ├── router.hpp
│   ├── router.cpp
│   ├── network.hpp
│   ├── network.cpp
│   ├── traffic_event.hpp
│   ├── config_loader.hpp
│   └── config_loader.cpp
└── docs/
    ├── relatorio/
    ├── slides/
    └── diagramas/