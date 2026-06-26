# Rede em Chip SPIN (modelo simplificado) - SystemC

Modelo simplificado de uma rede em chip inspirado na arquitetura **SPIN**,
implementado em **SystemC/C++**. A rede tem 8 roteadores em árvore gorda
quaternária (K4,4) de dois níveis e 16 terminais.

> Este é um **modelo didático simplificado**. Ele reproduz a topologia e o
> roteamento conceitual da SPIN, mas **não** implementa wormhole real por flits,
> créditos reais nem arbitragem round-robin completa. Veja [Limitações](#limitações).

## Objetivo

Simular pacotes trafegando entre os terminais da rede, registrando em log:

- caminho percorrido (rota de roteadores);
- entrega, descarte e pendência dos pacotes;
- latência (em ciclos) e número de saltos dos pacotes entregues;
- ocupação dos buffers por ciclo.

## Topologia

Árvore gorda K4,4 de 2 níveis (8 roteadores, 16 terminais):

```txt
        [R4]   [R5]   [R6]   [R7]      <- nível topo
          \  X   \  X   /  X  /
        [R0]   [R1]   [R2]   [R3]      <- nível folha
         |      |      |      |
       T0-3   T4-7   T8-11  T12-15     <- terminais
```

- **R0-R3**: roteadores folha. Cada um conecta 4 terminais nas portas lower.
- **R4-R7**: roteadores de topo. Cada folha liga suas 4 portas upper a todos os topos.
- Roteamento por menor caminho: o pacote **sobe** (adaptativo) até um topo comum
  e **desce** (determinístico) até a folha de destino.

## Dependências

- CMake >= 3.16
- Compilador C++17 (g++/clang++)
- **SystemC 2.3.x** (Accellera), localizável via `pkg-config`

### Instalando o SystemC no Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
sudo apt install libsystemc-dev     # fornece systemc.pc para o pkg-config
```

Confirme a instalação:

```bash
pkg-config --modversion systemc
```

Se o pacote da distribuição não existir, compile o SystemC a partir do fonte da
Accellera e garanta que `systemc.pc` esteja no `PKG_CONFIG_PATH`.

## Como compilar

```bash
cmake -S . -B build
cmake --build build
```

O executável é gerado em `build/spin_network`.

## Como executar

```bash
# Uso: ./build/spin_network [topologia] [trafego] [saida] [ciclos]
./build/spin_network                                  # usa os defaults
./build/spin_network input/topology_8.txt input/traffic_1.txt output/simulation_log.txt 20
```

Defaults: `input/topology_8.txt`, `input/traffic_1.txt`,
`output/simulation_log.txt`, `20` ciclos.

A simulação para antecipadamente quando todos os eventos foram injetados e a
rede está vazia; caso contrário, roda até `ciclos`.

### Tráfego de estresse

```bash
./build/spin_network input/topology_8.txt input/traffic_stress.txt output/stress_log.txt 30
```

## Onde fica o log

No arquivo passado como 3º argumento (default `output/simulation_log.txt`).
O log contém o banner, a topologia, o passo a passo por ciclo e o **relatório
final** com: pacotes entregues, descartados, **pendentes**, latência média e
média de saltos dos entregues.

## Formato de `traffic_*.txt`

Linhas após o marcador `[traffic]`, uma por pacote. Comentários começam com `#`.

```txt
[traffic]
# <ciclo> <terminal_origem> <terminal_destino>
0 0 15
1 5 10
```

## Formato de `topology_8.txt`

```txt
[routers]
leaf 0          # roteador folha (id 0)
top  4          # roteador de topo (id 4)

[terminals]
0 0 0           # <terminal> <roteador_folha> <porta_lower>
```

As ligações folha↔topo (full-mesh) são construídas automaticamente a partir
das contagens de `leaf`/`top`.

## Limitações

Este modelo é **simplificado** e honesto quanto ao que faz:

- **Pacotes são unidade atômica**: não há flits; o "wormhole" da SPIN não é
  reproduzido por flits reais.
- **Controle de fluxo simplificado** por capacidade de buffer. Há contadores de
  crédito, mas o backpressure real é dado pela capacidade dos buffers de entrada
  e dos buffers centrais QDN/QUP (usados como overflow).
- **Arbitragem/round-robin simplificados**: a seleção de porta usa crédito como
  proxy, sem arbitragem round-robin completa entre fluxos concorrentes.
- Pacotes que não cabem em nenhum buffer são **descartados** e os que sobram nos
  buffers ao fim da simulação são listados como **pendentes** — nenhum pacote
  desaparece silenciosamente.

## Estrutura

```txt
.
├── CMakeLists.txt
├── README.md
├── input/
│   ├── topology_8.txt
│   ├── traffic_1.txt
│   ├── traffic_2.txt
│   └── traffic_stress.txt
├── output/
│   └── simulation_log.txt
├── src/
│   ├── main.cpp
│   ├── packet.{hpp,cpp}
│   ├── router.{hpp,cpp}
│   ├── network.{hpp,cpp}
│   ├── traffic_event.hpp
│   └── config_loader.{hpp,cpp}
└── docs/
    └── relatorio/
```
