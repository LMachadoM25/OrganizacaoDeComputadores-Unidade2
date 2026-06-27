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
│   ├── traffic_1.txt
│   └── traffic_2.txt
├── output/
│   ├── simulation_log.txt
│   └── stress_log.txt
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
```

## Compilação

Na raiz do projeto, configure e compile com CMake:

```bash
cmake -S . -B build
cmake --build build
```

O executável gerado é `build/spin_network`.

## Execução

O programa recebe três argumentos: arquivo de tráfego, arquivo de saída e número máximo de ciclos.

```
./build/spin_network <arquivo_de_trafego> <arquivo_de_saida> <max_ciclos>
```

### Cenário principal

5 pacotes enviados entre terminais de folhas diferentes:

```bash
./build/spin_network input/traffic_1.txt output/simulation_log.txt 20
```

Resultado esperado: 5 pacotes entregues, 0 descartados, latência média de 3 ciclos, throughput de ~0,238 pacotes/ciclo.

### Cenário de estresse

10 pacotes injetados simultaneamente, testando contenção de buffers:

```bash
./build/spin_network input/traffic_2.txt output/stress_log.txt 30
```

Resultado esperado: 10 pacotes entregues, 0 descartados, latência média de 3 ciclos, throughput de ~0,323 pacotes/ciclo.

## Topologia

A topologia é fixa e definida diretamente no código: 8 roteadores e 16 terminais em arranjo K4,4 (fat-tree). Os roteadores 0–3 são folha e os roteadores 4–7 são de topo.

## Limitações

- Pacotes são tratados como unidades atômicas — não há divisão em flits.
- Wormhole não é implementado em nível físico.
- Controle de fluxo baseado em capacidade de buffer, sem mecanismo completo de créditos.
- Arbitragem simplificada (round-robin).
