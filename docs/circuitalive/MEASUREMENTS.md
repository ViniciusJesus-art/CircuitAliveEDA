# Probes, medições e osciloscópio

O painel **Simulation** inicia recolhido. Abra-o pela seta diagonal no final
da barra direita; outro clique cancela a colocação e recolhe o painel.
Ele contém a aba **Probes**.
Escolha Voltage, Current, Power ou Oscilloscope e clique em um fio elétrico.
O símbolo é uma seta diagonal para cima e para a direita, com V, A, W ou ~.
É possível colocar várias probes seguidas; **Esc** encerra a ferramenta.

O nome pode ser preenchido antes da colocação. Se estiver vazio, a sessão usa
a label da rede; sem label, usa a sequência A, B, C, …, Z, AA, …, conforme a
ordem de criação. Uma probe nomeada também ocupa sua posição na sequência.
Nomes repetidos recebem um sufixo numérico. Para renomear uma probe já colocada,
selecione-a e use **Ctrl+Alt+M**. O nome efetivo aparece junto da seta durante
a simulação. Componentes medidos pela janela de modelo usam sua referência,
por exemplo R1 ou D1.

## O que cada instrumento mede

| Ferramenta | Grandeza e ligação |
|---|---|
| Voltage (V) | Tensão do fio em relação à referência elétrica da ilha |
| Current (A) | Corrente no trecho escolhido, positiva da esquerda para a direita ou de cima para baixo |
| Power (W) | Tensão do fio em relação à referência multiplicada pela corrente no trecho |
| Oscilloscope (~) | Tensão do fio em função do tempo, em relação à referência |

GND/0 explícito define a referência; sem ele, é usado o negativo de uma fonte
DC por ilha, como no restante da simulação. O sentido positivo das probes A/W
é fixo nesta versão; uma leitura negativa indica corrente no sentido oposto.

Probes A/W inserem uma fonte ideal de 0 V em série, sem resistência de carga.
O trecho precisa ser horizontal ou vertical e ter espaço para os dois terminais,
sem junções, labels ou pinos dentro do espaço ocupado pelo sensor. O programa
recusa probes cujos terminais sejam unidos por outro fio ou pela mesma label,
pois esse desvio impediria medir a corrente do trecho escolhido.

**A potência da probe no fio é V(referência) × I do ramo.** Para medir a
potência absorvida por um resistor ou LED, marque P no próprio componente:
ela usa a queda de tensão entre os seus terminais.

As probes são símbolos nativos incorporados ao `.kicad_sch`; salvar e reabrir
preserva desenho, conexão, nome e tipo. Não entram em BOM, placa ou arquivos de
posição. Inserir uma probe é uma única operação de desfazer. Excluir uma probe
A/W pelo Delete normal deixa o espaço entre seus dois terminais: reconecte o
fio ou desfaça a inserção para recuperar o trecho original.

## Medir no componente

1. Selecione o componente e abra **Simulation model…** com **Ctrl+Alt+M**.
2. Marque **Measure V**, **Measure I** e/ou **Measure P**.
3. Confira **From (+)** e **To (−)**, os terminais lógicos do arquivo de modelo.
4. Confirme, salve e pressione Play.

V é a tensão From menos To. I entra pelo terminal From. P = V × I segue a
convenção passiva: positivo significa potência absorvida; negativo, fornecida.
Os arquivos distribuídos já indicam o par padrão; no LED, ânodo e cátodo.
Em modelos com vários terminais, essa leitura se refere ao par escolhido;
não soma automaticamente a potência de todas as portas do componente.

As seleções ficam no campo oculto `CA.Measurements` do símbolo, em JSON:

```json
{"voltage":true,"current":true,"power":true,"positive":"a","negative":"k"}
```

Cada componente mantém suas próprias escolhas, com suporte ao desfazer/refazer.
Mudar essas opções encerra a sessão anterior; Play reconstrói o circuito.

## Abas durante a execução

- **Measurements** abre automaticamente com Play se houver probes V/A/W ou
  medições marcadas em componentes. Mostra nome, unidade e valor instantâneo,
  incluindo prefixos m, µ, k etc. Uma probe desconectada mostra `—`.
- **Oscilloscope** só aparece quando existe uma probe desse tipo no esquema.
  Se ela for o único instrumento, sua aba é selecionada automaticamente.
  Várias probes de osciloscópio geram curvas com cores diferentes no mesmo gráfico.
- Pause congela o tempo, as leituras e as curvas. Play retoma a sessão;
  Stop descarta os dados e recolhe as duas abas. A aba Probes permanece disponível.

O osciloscópio oferece janelas de 1, 10, 50, 100, 250 e 500 ms, com escala de
tensão automática. O eixo horizontal usa tempo de simulação. O texto abaixo
informa a quantidade e o intervalo de amostras disponíveis. O histórico é
limitado; com muitos sinais, uma janela de 500 ms pode conter dados apenas em
parte dela. O desenho conserva os extremos das amostras agrupadas por pixel.

O passo máximo atual do solver é **100 µs**. Este osciloscópio inicial serve
para acompanhar transientes compatíveis com essa resolução; ainda não oferece
trigger, cursores, RMS, FFT ou captura de sinais de alta frequência. A sessão
aceita até 256 sinais salvos, incluindo indicadores e monitores internos.

## Status e avisos

O rodapé mostra estado, tempo e avisos. As correntes dos LEDs foram transferidas
para as medições opcionais por componente.

As fontes DC distribuídas têm limite de aviso de corrente de 10 A por padrão,
configurável em **Simulation model… → Current warning**; 0 desativa esse aviso.
O LED conserva seu limite configurável, inicialmente 20 mA. Exceder um limite
em uma amostra gera aviso com a referência do componente, conservado até Stop,
mesmo que uma chave seja aberta depois. O aviso não limita a corrente nem
implementa dano térmico. Um curto direto entre os terminais da fonte DC é
identificado antes do início; fechar uma chave sobre uma fonte pode gerar
sobrecorrente ou erro do solver. Não há detecção universal de falhas físicas.

## Exemplos e organização

```bash
./circuitalive eeschema demos/simulation/circuitalive/measurement_probes.kicad_sch
./circuitalive eeschema demos/simulation/circuitalive/scope_rc.kicad_sch
```

O primeiro inclui as quatro ferramentas e medições de R1/D1. O segundo mostra
a carga de C1; clique em SW1 para acompanhar sua descarga no osciloscópio.

O código continua isolado em `eeschema/circuitalive/`: `measurements.*` define
canais e unidades; `probe_placement.*` cria símbolos e modifica fios usando o
histórico nativo; `instrument_panel.*` cuida das abas e do gráfico. O adapter
captura os símbolos, o snapshot cria os sensores e o runner coleta os valores
e o histórico fora da thread gráfica. Nenhum formato do KiCad foi alterado.

Validação: 42 casos do núcleo interativo e 61 casos SPICE, incluindo direção
da corrente, balanço de potência, nomes, probes em arquivos salvos, carga RC,
histórico limitado, pausa/retomada, avisos e pinagem física. A interface foi
verificada em sessão Xvfb isolada com colocação, gravação e reabertura.
