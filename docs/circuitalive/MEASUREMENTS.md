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

## Osciloscópio

**Time/div** controla o tempo de cada uma das dez divisões horizontais. A lista
usa a sequência 1–2–5; também aceita valores digitados com unidade, como
`250 us`, `2 ms` ou `0,05 s`, aplicados com Enter ou ao sair do campo.
A faixa manual é 1 ns/div a 1000 s/div; isso controla a apresentação,
não muda o passo do solver nem aumenta o histórico disponível.
A roda do mouse sobre o gráfico muda Time/div; **Shift + roda** muda a escala
do sinal selecionado. **Expand / dock** abre o painel em uma janela maior e
permite devolvê-lo à lateral. Também é possível redimensionar a janela.

**Auto** ajusta a amplitude de cada sinal, centraliza o nível de trigger e,
quando há ciclos suficientes, escolhe uma escala de tempo para alguns períodos.
A janela escolhida é limitada ao histórico disponível naquele momento.
O desenho preserva mínimo e máximo de cada grupo de amostras por pixel.

### Trigger

Na aba **Trigger**, escolha a fonte, a borda de subida/descida e o nível em V.
A linha laranja horizontal mostra o nível na escala do canal de trigger;
a linha vertical T marca o instante de disparo. Os tempos do eixo e dos
cursores passam a ser relativos a T.

| Modo | Aquisição |
|---|---|
| Auto | Dispara pela borda; sem disparo por uma janela de tempo, mostra uma captura livre |
| Normal | Atualiza somente depois de uma borda válida e da aquisição do intervalo posterior; conserva a última captura enquanto aguarda |
| Single | Arma para a próxima borda e conserva uma única captura; **Rearm** espera uma nova borda |
| Off / roll | Atualiza continuamente, sem sincronização por trigger |

**Hysteresis** exige que o sinal volte além do nível antes de armar outra borda,
reduzindo disparos por ruído. O disparo usa interpolação linear entre as duas
amostras da borda; não depende de uma amostra atingir exatamente o nível.
**Pretrigger (%)** posiciona o disparo na janela; 50% reserva metade do intervalo
para antes dele. **Holdoff** define o tempo mínimo entre o fim de uma captura
e a aceitação da próxima borda. **Time offset** desloca a janela em segundos:
positivo para depois, negativo para antes. No modo roll, valores negativos
permitem examinar uma parte anterior do histórico ainda disponível.

**Hold** congela apenas a captura do osciloscópio, enquanto o circuito continua
simulando. É possível mudar a escala de tempo, posição e amplitude durante Hold
ou depois de Single sem perder a captura. Sair de Hold ou usar Rearm retoma a
busca a partir das amostras mais recentes. O Pause da barra superior congela
a própria simulação e todas as medições.

### Escala de cada sinal

Na aba **Signals**, escolha o sinal e ajuste **Amplitude / div**, **Center (V)**,
visibilidade e inversão do desenho. O centro é a tensão da linha central da grade
de oito divisões verticais. Cada canal tem sua própria escala; as cores e as
legendas identificam essas escalas. Os valores no eixo vertical correspondem
ao sinal selecionado. O pequeno traço à esquerda indica o zero de cada canal.
Editar escala ou centro desliga o ajuste automático desse canal;
**Fit this signal** volta ao automático. Inverter o desenho não troca o sinal
das medições nem altera o circuito.

### Cursores

Na aba **Cursors**, escolha **Time**, **Amplitude** ou **Time + amplitude** e o
sinal de referência. Arraste as linhas no gráfico ou digite os valores A e B.
Um clique posiciona A; Shift-clique posiciona B. Perto de uma linha, o arraste
move essa linha. Nos modos de tempo, os campos usam o mesmo zero do eixo.

- Cursores verticais: tensão interpolada em A/B, **Δt**, **ΔV(t)**,
  **1/|Δt|** e **ΔV/Δt**. O inverso de Δt representa uma frequência somente
  quando os cursores delimitam um período.
- Cursores horizontais YA/YB: níveis manuais e **ΔY**, diferença de amplitude.
- Um resumo permanece sob o gráfico mesmo ao trocar a aba de controles.

As leituras usam a escala física do canal escolhido. Fora do intervalo
realmente adquirido, mostram `—`; não extrapolam partes ausentes do histórico.

### Medições dos sinais

A aba **Readings** mostra todos os sinais do osciloscópio, mesmo os ocultos:
**mínimo, máximo, pico a pico, média, RMS, frequência, período, duty cycle** e
quantidade de amostras. Selecionar uma linha escolhe esse canal para o eixo e
os cursores. Na lateral estreita, use a rolagem horizontal da tabela ou
**Expand / dock** para ver todas as colunas.

Média e RMS são ponderados pelo tempo, integrando a interpolação linear entre
amostras; RMS inclui a componente DC. Os cálculos usam apenas a parte disponível
da janela visível, incluindo suas fronteiras interpoladas. Frequência e período
são estimados por bordas de subida no ponto médio entre mínimo e máximo, com
histerese de 10% da amplitude. Exigem pelo menos duas bordas de subida e aparecem como estimativas.
São úteis para sinais periódicos; transientes não periódicos podem produzir
uma estimativa sem significado físico.
O duty cycle mede o tempo acima desse ponto médio entre a primeira e a última
borda de subida. Sinais constantes ou sem ciclos suficientes mostram `—`.

**Export capture CSV…** grava tempo absoluto de simulação em segundos e tensões
em volts, usando as amostras brutas da captura visível. A exportação usa uma
cópia consistente da captura, mesmo se a simulação continuar durante a escolha
do arquivo. Os ajustes de escala, canais, trigger e cursores sobrevivem a
Stop/Play durante a mesma janela do editor; os dados de aquisição são descartados
por Stop. Esses ajustes ainda não são gravados no projeto ou entre execuções.

### Resolução e limites

O texto inferior informa o passo máximo do solver, o maior intervalo entre
amostras da captura e o tamanho do histórico disponível. O passo padrão é
100 µs, reduzido automaticamente por determinados modelos, como fontes AC e 555.
O histórico contém até 8192 amostras e 65536 valores; mais sinais salvos reduzem
a duração disponível. Capturas que excedem esse espaço indicam **partial history**
e deixam as regiões ausentes sem desenho. O limite também vale durante Single.

Zoom não recupera amostras inexistentes. O painel sinaliza espaçamento excessivo
para a escala escolhida ou para o período estimado. A frequência detectada pode
sofrer aliasing; essa indicação não detecta todos os casos. Para frentes muito
rápidas ou alta frequência, será necessário ampliar os controles de resolução
da aquisição/solver. Esta etapa não inclui FFT nem simulação de largura de banda,
ruído ou carga de uma probe física. A sessão aceita até 256 sinais salvos,
incluindo indicadores e monitores internos.

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
histórico nativo; `instrument_panel.*` cuida das abas; `scope_panel.*` reúne os controles e o
desenho; `scope_acquisition.*` implementa captura, trigger, interpolação, escalas
e cálculos sem depender de wxWidgets. O adapter
captura os símbolos, o snapshot cria os sensores e o runner coleta os valores
e o histórico fora da thread gráfica. Nenhum formato do KiCad foi alterado.

Validação do osciloscópio: testes de pré/pós-trigger, bordas, histerese,
holdoff, Single/rearme, Auto/Normal/Roll, Hold e zoom, sobreposição de históricos,
reinício de sessão, memória limitada, integração de média/RMS com amostragem
irregular, frequência/duty, cursores e entradas SI. A interface é conferida
em Xvfb isolado com o exemplo `ne555_astable.kicad_sch`.
