# Paralelismo da simulação: micros e partições analógicas

## Decisão

É viável executar microcontroladores em paralelo, cada um com estado e contexto
isolados. Os ensaios abaixo confirmam ganho com vários ATmegas. A divisão por
página depende das conexões: uma folha é uma organização do desenho, enquanto a
unidade de cálculo precisa respeitar todas as dependências elétricas e lógicas.

A recomendação é combinar janelas cooperativas, eventos com tempo simulado e
workers persistentes limitados à capacidade da CPU. Criar uma thread por folha
ou trocar mensagens a cada instrução não resolve o custo de sincronização.

O coordenador está integrado ao worker do aplicativo. Cada contexto mantém sua
fronteira e seus eventos privados, e janelas suficientemente caras podem rodar
em paralelo. O controlador também distribui blocos analógicos comprovadamente
independentes entre processos ngspice persistentes. O benchmark opcional de MCU
mede o mesmo coordenador de produção; a auditoria nativa descrita abaixo compara
o circuito completo com um solver e com as partições.

## Particionamento analógico implementado

`BuildCircuitSnapshot` registra os intervalos do netlist que pertencem à expansão
de cada componente. `BuildAnalogPartitions` usa essa proveniência e as dependências
dos elementos SPICE: nós analógicos, nós de eventos digitais, tensões observadas
em expressões, correntes de ramos, indutores acoplados, memórias e entradas de MCU.
Toda a expansão de um componente permanece indivisível. Bobina, contatos, estado
interno e atraso de um relé, por exemplo, continuam no mesmo solver, mesmo quando
seus terminais pertencem a grupos galvanicamente isolados.

Terra e fontes ideais DC diretamente referenciadas ao terra podem ser fronteiras.
As fontes são replicadas nas partições; suas correntes observadas são somadas.
Uma resistência de alimentação, fio de sinal, realimentação ou expressão que
observe a corrente total de uma fonte mantém os blocos dependentes juntos.
Fontes flutuantes também permanecem no domínio conectado. Não se corta um ramo
para trocar tensões aproximadas por mensagens entre solvers.

A análise só é habilitada para decks certificados pelo compilador. Sintaxe fora
do subconjunto reconhecido, subcircuitos e modelos com parâmetros alteráveis em
tempo real conservam o solver único. Dependências não comprovadas nunca autorizam
uma separação. Isso pode deixar oportunidades de paralelismo sem aproveitar.

`ANALOG_SIM_SESSION`, usado por `SESSION_RUNNER`, mantém um processo isolado e uma
thread de comunicação persistente por partição. Não compartilha o estado global
do ngspice nem do módulo nativo entre processos. O limite automático é de quatro
processos e de `max(1, CPUs lógicas - 2)`. Somente ilhas com pelo menos 16 elementos
justificam abrir outro processo automaticamente; ilhas menores são distribuídas
entre os trabalhos existentes. O limite explícito da API é usado nos testes.

Cada pedido de avanço tem a mesma barreira absoluta em todos os workers. O passo
analógico é limitado nessa barreira, inclusive nos blocos sem MCU. A margem da
condição de parada e a margem do limitador de passo são consistentes: isso evita
um resto de poucos attossegundos e o erro `Timestep too small` na retomada de uma
memória. A publicação verifica a coincidência das fronteiras dentro da margem
numérica, normalmente até 20 ps. Não se aumenta o passo máximo configurado nem
se altera o clock do firmware.

Últimos valores, índices dos monitores e regiões de memória são mapeados de volta
à ordem global. Capturar uma memória consulta o processo que executou suas escritas.
Alterações de fontes replicadas são aplicadas em todos os destinos pausados antes
de qualquer retomada. Todos os jobs terminam antes da propagação de uma falha;
reconfiguração e destruição recolhem threads e processos. Circuitos que continuam
com um solver usam o caminho síncrono direto, sem despacho por lote.

O histórico publicado reúne os timestamps dos anéis locais. Os valores de cada
canal são preservados nos seus próprios timestamps; valores dos outros blocos são
interpolados linearmente para formar o vetor de observação, incluindo a corrente
total de fontes replicadas. Essa interpolação serve a medições e apresentação e
nunca alimenta os solvers ou o firmware. Os passos adaptativos de cada bloco podem
ser diferentes dos passos do solver monolítico; não se promete igualdade bit a bit
dos transientes numéricos. Mantêm-se a seleção de canais e o limite de cada anel;
a união intermediária tem no máximo workers × capacidade local de timestamps.
Os buffers de longo prazo do osciloscópio continuam com seus limites anteriores.

O protocolo IPC passa para a versão 4 para exigir suporte à barreira analógica.
O contrato JavaScript dos backends continua na versão 1. Aplicativo e worker devem
ser atualizados juntos e o aplicativo precisa ser reiniciado.

O pacing preserva até 100 ms de atraso ocasional para recuperação por lotes
seguintes mais baratos, retirando somente esperas ociosas. Atrasos maiores são
descartados para não acumular dívida após inicialização, suspensão ou depuração.
Os comandos continuam sendo verificados entre lotes. Essa recuperação permite
usar a folga média do solver para sustentar 1× depois de um transiente caro;
não altera os avanços simulados nem torna um motor sem capacidade mais rápido.

### Ensaio integrado com Blink de 200 ms

O projeto capturado nesta etapa contém 21 componentes, 165 sinais, um ATmega328P,
relé, transistor, diodos, duas memórias e lógica. O plano automático usa dois
solvers: o bloco de memória/lógica e o bloco do ATmega/relé, com as ilhas pequenas
distribuídas entre eles. As fontes ideais compartilhadas são replicadas. Firmware,
netlist de origem, passo máximo de 100 µs e relógio de 16 MHz são os mesmos nos
dois modos. O projeto original não foi alterado.

Três repetições com os canais de histórico usados para avisos e instrumentos,
avançando em lotes absolutos de 8 ms, após a inicialização:

| Modo | Tempo simulado | Mediana de processamento | Capacidade sem pacing |
|---|---:|---:|---:|
| Um solver | 1,208 s | 2,499 s | 0,483× |
| Duas partições | 1,208 s | 1,019 s | 1,186× |

O ganho de processamento medido foi 2,45×. Em 151 snapshots por repetição, a
diferença máxima de tensão foi 2,69 µV e de corrente foi 49,7 nA. Esses números
descrevem os instantes comparados, não um limite provado para todo transiente.

No `SESSION_RUNNER` de produção, incluindo monitores de atividade, avisos,
atualização visual e pacing de 1×, o ensaio de 2,4 s simulados mediu:

| Modo | Velocidade após inicializar | Maior intervalo entre atualizações |
|---|---:|---:|
| Um solver | 0,480× | 264,7 ms |
| Duas partições e recuperação de atraso | 0,998× | 35,4 ms |

As quatro janelas de medição de aproximadamente 500 ms do segundo caso ficaram
entre 0,975× e 1,024×. As doze mudanças visuais de PB5 tiveram os mesmos níveis e
tempos simulados nos dois modos; o limite visual continua sendo o intervalo de
atualização, não os ciclos do firmware. Essa auditoria exercita o controlador da
interface e a captura nativa do esquema, sem medir o custo de renderização wx.
Os números dependem da máquina e não garantem 1× para outros projetos.

Uma execução prolongada de 6,4 s simulados manteve 0,999× após inicializar, com
801 publicações e intervalo máximo de 39,7 ms, sem erro de timestep. As janelas
de 500 ms variaram de 0,944× a 1,049×, recuperando os atrasos dos transientes.
O worker instalado também coincidiu com o compilado nos cinco snapshots do
ensaio de instalação, com diferença máxima medida zero.

Passaram 144 testes de regressão do worker/controlador e dois testes nativos de
captura/pinos/firmware. Os casos novos verificam partições, correntes somadas,
atividade de pinos de terra, fontes alteradas, escrita/leitura de duas memórias
independentes, relé com atraso, fronteiras de entrada do AVR, falhas, reinício,
histórico selecionado e recuperação de atrasos curtos. Os resultados locais estão
em `.circuitalive/experiments/analog-islands/`.

Auditoria opcional, explicitamente selecionada em `qa_spice`:

```sh
source tools/circuitalive/env.sh
export CIRCUITALIVE_AUDIT_SCHEMATIC=/caminho/projeto.kicad_sch
export CIRCUITALIVE_AUDIT_REPORT=/tmp/circuitalive-audit.json
CIRCUITALIVE_AUDIT_PARTITIONS_UNTIL=1.21 xvfb-run -a \
  build/circuitalive/qa/tests/spice/qa_spice --run_test=AuditSavedInteractiveCircuit
```

Para medir o controlador, use `CIRCUITALIVE_AUDIT_LIVE_UNTIL=2.4` no lugar de
`CIRCUITALIVE_AUDIT_PARTITIONS_UNTIL`. `CIRCUITALIVE_AUDIT_NO_PARTITIONS=1` força
o solver único somente nessa auditoria. O relatório inclui as janelas de
velocidade, mudanças de PB5, netlist, firmware congelado e plano de partições.

## Funcionamento implementado

- `PROGRAMMABLE_RUNTIME` mantém um QuickJS-NG por micro, uma fronteira de cálculo
  e no máximo uma janela de eventos futuros por contexto. `Time()` é a fronteira
  comum confirmada; `States()` e os eventos retornados só publicam até esse tempo.
- Somente os contextos que chegaram à fronteira comum recebem novas entradas e
  executam outra janela. Um contexto adiantado conserva seu trabalho até que o
  solver alcance sua própria barreira. Isso evita recalculá-lo a cada amostra
  de um micro que esteja consultando GPIO continuamente.
- Um executor persistente inclui a thread chamadora e até
  `min(micros, processadores_lógicos - 2)` executores no modo automático, com
  mínimo de um executor. Trechos com até oito quanta e trabalhos medidos abaixo
  de 20 µs continuam sequenciais; o paralelismo é usado quando pelo menos duas
  janelas justificam o despacho. As decisões afetam somente o desempenho.
- Eventos simultâneos preservam a ordem de configuração e a ordem do backend.
  Todos os jobs terminam antes de propagar a primeira falha nessa mesma ordem.
  Um runtime que falhou exige Reset/Configure antes de continuar.
- `PROGRAMMABLE_PIPELINE` sobrepõe uma chamada do coordenador ao solver quando
  a fronteira não precisa de entradas novas. O trabalho permanece dentro do
  prazo do lote atual da interface; Reset/Stop recolhem o trabalho pendente.
- A compilação permite até 1 ms cooperativo para cada modelo com alimentação e
  reset constantes, inclusive com vários micros. O AVR devolve o controle antes
  de observar entradas. Ao habilitar ADC, RX, interrupção ou clock externo, a
  janela passa a respeitar o quantum de amostragem imediatamente.
- Backends sem `prepareWindow`, ou com alimentação/reset variáveis, conservam
  sua amostragem. As entradas são validadas e distribuídas por contexto antes
  de iniciar qualquer job, evitando varrer os pinos dos demais em cada runtime.
- Mudanças de fonte/parâmetro invalidam as tensões instantâneas antigas para
  lookahead: a próxima janela usa somente um quantum e não gera prefetch; o
  solver resolve as novas tensões antes do próximo avanço cooperativo.
- Prazos coincidentes acumulados por contextos diferentes são normalizados
  somente dentro do erro de representação em ponto flutuante. O quantum
  elétrico não é usado para arredondar timestamps de eventos.
- O adaptador reúne os componentes e conexões de todas as folhas ativas antes
  da análise das partições. A posição gráfica da folha não define um domínio.

O contexto QuickJS pode mudar de thread sequencialmente, atualizando seu stack
top. Dois workers nunca devem acessar simultaneamente o mesmo contexto ou
objetos do interpretador. A implementação vendorizada é QuickJS-NG 0.17.0;
separar runtimes também separa CPU, SRAM, periféricos e orçamento de execução.

## Medições do coordenador integrado

No mesmo processador, três repetições por caso, a versão integrada foi comparada
ao worker instalado anterior. O circuito contém micros ATmega328P de 16 MHz,
firmware Blink de 200 ms, alimentação/reset ideais e cargas de 10 kΩ. As versões
recebem o mesmo netlist, firmware, módulo JavaScript, amostragem de 1 µs e pedidos
de avanço de 8 ms. São tempos de processamento sem o pacing de 1× da interface;
não incluem renderização ou transferência do histórico do osciloscópio.

| Micros | Tempo simulado | Worker anterior | Coordenador integrado | Ganho |
|---:|---:|---:|---:|---:|
| 2 | 648,1 ms | 9,182 s | 0,188 s | 48,7× |
| 4 | 72,1 ms | 2,090 s | 0,111 s | 18,8× |

As tensões dos pinos, fontes e a corrente total coincidiram em todos os snapshots
comparados, com diferença máxima medida zero. O ensaio de dois micros atravessa
os flancos de aproximadamente 200, 400 e 600 ms. O caso de quatro micros tem
intervalo menor e inclui o custo inicial da CPU/solver. O ganho combina eliminar
barreiras, preservar trabalho entre fronteiras e usar o pool; não é um ganho
atribuível somente ao número de threads nem uma medida do projeto completo do usuário.

O benchmark de runtime agora chama o coordenador real. Em 20 ms simulados,
janelas cooperativas de até 1 ms e três repetições, a mediana foi:

| Micros | Coordenador sequencial | Pool automático | Ganho do pool |
|---:|---:|---:|---:|
| 1 | 4,128 ms | 4,147 ms | 1,00× |
| 2 | 8,166 ms | 7,043 ms | 1,16× |
| 4 | 16,271 ms | 8,447 ms | 1,93× |
| 8 | 33,749 ms | 10,900 ms | 3,10× |

As 72 medições preservaram as transições da referência sequencial de 1 µs.
Janelas de entrada de 1 µs mantêm o caminho sequencial quando o despacho não
compensa. Os contadores registraram uso efetivo do pool nas janelas maiores.

A imagem congelada do projeto original com um ATmega também foi comparada:
1.192,1 ms simulados, três repetições, mediana de 1,694 s antes e 1,712 s depois,
com diferença máxima zero nas tensões/correntes comparadas. Esse caso já usava
execução cooperativa e teve desempenho praticamente estável; o ganho principal
desta implementação está nos projetos com vários micros.

Validação integrada: 79 casos de `qa_interactive_sim` e dois casos nativos de
`qa_spice` passaram, incluindo histórico GPIO entre micros, alimentação variável,
AVR junto de PIC, UART/INT0 elétricos, falhas, reset e precisão de parada com dois
contextos após 128 s simulados. As verificações JavaScript também confirmaram
65.536 opcodes em dois estados e 190.001 fronteiras de temporização. A comparação
estendida com oito instâncias preservou os eventos dos firmwares de Blink de
200 e 500 ms durante 650 ms simulados.

## Medições do protótipo inicial

Máquina: Intel Core 5 210H, 8 núcleos físicos / 12 processadores lógicos, Linux.
Mesmas bibliotecas de runtime da compilação do aplicativo. Sem fixar afinidade
de CPU; três repetições, mediana do tempo de execução. Inicialização dos
contextos e criação das threads ficam fora do cronômetro.

O teste replica um firmware ATmega328P de Blink a 16 MHz, com 27 pinos por
instância, alimentação/reset constantes e demais entradas constantes. Cada
micro tem seu próprio runtime tanto na versão sequencial quanto na paralela.
A versão paralela usa uma thread persistente por instância e barreiras com
mutex/condition variable. Não envolve ngspice, osciloscópio ou renderização.
Os tempos abaixo são para **60 ms de execução dos micros**, não para a
simulação completa do projeto.

| Micros | Lote | Sequencial | Paralelo | Ganho |
|---:|---:|---:|---:|---:|
| 1 | 1 ms | 10,8 ms | 12,9 ms | 0,83× |
| 2 | 1 ms | 22,3 ms | 14,6 ms | 1,52× |
| 4 | 1 ms | 43,8 ms | 16,9 ms | 2,59× |
| 8 | 1 ms | 87,2 ms | 19,3 ms | 4,51× |
| 4 | 100 µs | 56,3 ms | 27,4 ms | 2,06× |
| 4 | 1 µs | 1.448,8 ms | 1.009,1 ms | 1,44× |

No caso de quatro micros, apenas reduzir a frequência de chamadas de 1 µs para
1 ms diminuiu o tempo sequencial de 1.448,8 para 43,8 ms. Acrescentar paralelismo
ao lote de 1 ms trouxe outro ganho de 2,59×. Portanto, eliminar sincronizações
desnecessárias deve preceder a multiplicação de threads. O resultado de um
micro não mede o benefício do pipeline existente, pois neste ensaio não há
solver analógico para executar em paralelo.

**O lote de 1 ms é uma condição experimental com entradas constantes.** Ele não
autoriza trocar o quantum de todos os modelos para 1 ms. Uma leitura de GPIO ou
uma interrupção pode precisar de uma entrada muito antes disso. Congelar essas
entradas indiscriminadamente altera a lógica, a latência e as medições.

Validação do protótipo:

- 72 medições do firmware externo: 1/2/4/8 micros, lotes de 1/100/1000 µs,
  sequencial/paralelo e três repetições. Todos os eventos de cada instância
  reproduziram pino, nível e timestamp da referência de 1 µs, com tolerância
  de 1 ps. Firmware SHA-256:
  `56b28b023f5bc8ed7190b61cf99255d858df0da34c12c388e3b39452d8cbda16`.
- 24 medições adicionais com firmware de instruções AVR incluído no benchmark,
  alternando PB5 aproximadamente a cada 192 µs. A referência usa o mesmo programa
  e entradas com execução sequencial de 1 µs.
- Auditoria com quatro micros encadeados: U1 lê uma onda externa em PD2; os
  demais leem PB5 da instância anterior. As entradas são fotografadas antes
  de qualquer micro avançar. As versões sequencial e paralela produzem as
  mesmas transições durante 1.000 barreiras de 1 µs.
- Verificação estendida de 650 ms com oito instâncias em paralelo: o firmware
  atual de Blink de 200 ms e a imagem congelada anterior de 500 ms mantiveram
  todas as transições da referência sequencial de 1 µs, inclusive os flancos
  em aproximadamente 200/400/600 ms e 500 ms, respectivamente.

Esses ensaios do protótipo validam o isolamento dos runtimes e o paralelismo em
barreiras conhecidas. Não validam a troca assíncrona arbitrária de eventos nem
uma garantia de 1× para qualquer projeto. A validação das partições analógicas é
feita separadamente, com o controlador integrado e o circuito completo.

## Arquitetura recomendada

1. **Um coordenador de tempo simulado.** Todos os motores recebem prazos
   explícitos. Eventos levam timestamp e ordem determinística; o relógio real
   controla somente o ritmo da interface. Ganhar desempenho não muda clocks,
   tempos de PWM, disparos do osciloscópio ou resultados das medições.
2. **Estado próprio por micro, workers persistentes em um pool limitado.**
   Cada contexto tem somente um executor ativo. Uma thread não significa um
   núcleo exclusivo. O pool evita criar dezenas de threads concorrendo com o
   ngspice e a interface; trabalho pequeno pode continuar sequencialmente.
3. **Janelas cooperativas por instância.** Um micro executa trabalho interno
   até precisar observar uma entrada ou alcançar seu prazo. O solver usa os
   eventos de saída nos instantes originais. A alimentação/reset precisam ser
   comprovadamente constantes para permitir o avanço atual sem reamostragem.
4. **Publicação ordenada e buffers limitados.** Se um micro parar em 10 µs e
   outro chegar a 1 ms sem ler entradas, o coordenador só confirma a fronteira
   causal permitida. Os resultados futuros do segundo ficam em seu buffer.
   Seus estados futuros não podem aparecer em `States()`, no desenho ou nas
   medições antes da hora. Ao chegar à sua leitura, ele recebe tensões do solver
   naquele instante. Pausa, controles e Stop respeitam limites confirmados.
5. **Partições por dependências para o analógico.** Circuitos comprovadamente
   independentes podem usar workers/instâncias ngspice separados. Se houver
   acoplamento, ficam juntos ou exigem um método de cossimulação validado.
   Correntes de uma alimentação ideal compartilhada e canais de medição
   precisam de agregação consistente quando houver separação.

A implementação mantém um tempo de cálculo por contexto e um tempo público
confirmado. Habilitar apenas `autonomousWindowSeconds` e escolher o menor retorno
não resolveria o problema: um contexto poderia já ter avançado além da leitura
do outro. Os buffers por instância retêm esses resultados futuros, e uma nova
leitura recebe entradas somente quando o solver alcança sua barreira.

Uma página pode compartilhar nets globais, pinos hierárquicos, alimentação com
impedância, fontes controladas ou componentes acoplados com outra folha. Dentro
da mesma página também podem existir blocos independentes. Terra ideal comum
não cria, sozinho, uma dependência dinâmica; uma fonte não ideal compartilhada
pode criá-la. A análise deve incluir equações e acoplamentos dos modelos, não
somente fios ou `electricalGroups`, que hoje servem à escolha de referência
elétrica e não são um certificado de independência para paralelização.

O ngspice oferece APIs para múltiplas instâncias sincronizadas, mas deixa o
particionamento, a comunicação e a convergência entre partições a cargo do
integrador. Cortar uma realimentação analógica e trocar tensões somente ao fim
de cada lote pode mudar correntes, estabilidade e cruzamentos de limiar.

## Prioridades e outras famílias

Esta implementação remove as barreiras desnecessárias entre vários micros e
distribui janelas caras ao pool. A validação cobre publicação de eventos futuros,
ordenação simultânea, GPIO, ADC, PWM, UART entre micros, interrupções externas,
reset, alterações de fonte, pausa/retomada, Stop e aquisição/medição. Outros
backends podem aderir ao mesmo contrato; não foram acrescentadas novas famílias.

A parte digital do XSPICE já usa eventos, assim como o agendamento dos
temporizadores do AVR. O motor deve continuar híbrido: eventos digitais e
execução interna em blocos, com integração analógica adaptativa nas regiões que
precisam resolver tensões e correntes. Tratar um circuito analógico inteiro
somente como mudanças de nível perderia transientes e cruzamentos de limiar.

O projeto com **um** ATmega já aproveita um worker cooperativo. Acrescentar uma
thread por micro não lhe dá outro núcleo útil automaticamente. Para esse caso,
a prioridade é medir o tempo restante no solver, nas pontes elétricas e no
backend, e reduzir avaliações/cópias sem ultrapassar barreiras de entrada.

STM/ESP devem aderir ao contrato de tempo, eventos e entradas, independentemente
do backend de CPU. Motores nativos com execução em blocos ou tradução dinâmica
podem ser mais adequados que interpretar instruções em JavaScript para famílias
maiores. Cada motor precisa validar instruções, periféricos e temporização.
Por exemplo, Renode já possui domínios de tempo e sincronização, mas sua taxa
configurável em MIPS não equivale automaticamente aos ciclos reais de cada MCU.
Ele é uma referência de arquitetura e possível integração, não uma substituição
já pronta para as medições elétricas atuais.

GPU não é a prioridade para estes circuitos: a variante CUSPICE do ngspice não
suporta XSPICE, usado nas pontes e modelos atuais, e foi voltada a circuitos
grandes. A própria documentação indica custo de comunicação que só compensa em
escala de milhares de transistores. Isso não impede avaliar outras soluções no
futuro, mas não constitui uma aceleração disponível para este motor.

## Reproduzir

```sh
tools/circuitalive/build.sh --target circuitalive-benchmark-programmable
source tools/circuitalive/env.sh
build/circuitalive/qa/tests/spice/circuitalive-benchmark-programmable - .06 3
```

O argumento `-` usa o Blink AVR incluído. Para repetir com outro firmware de
ATmega328P, substitua-o pelo caminho HEX/BIN. Os argumentos seguintes são a
duração simulada, em múltiplos de 1 ms, e o número de repetições. O programa
escreve JSON por linha e falha se as transições divergirem. Inclui contadores
de janelas, threads e lotes paralelos do coordenador real. Cada chamada também
executa a auditoria de quatro micros encadeados. Os resultados dependem do
firmware e da máquina. `cpu_seconds` usa `std::clock`; nesta medição Linux,
representa tempo de CPU do processo, enquanto `wall_seconds` mede tempo decorrido.

O alvo é `EXCLUDE_FROM_ALL`, não é instalado e não integra o `ctest`. Não há
limiar de velocidade nos testes automatizados. Como usa entradas constantes,
firmware dependente de entradas pode divergir entre lotes, o que não indica
necessariamente erro do backend; evidencia que o lote maior não preserva o caso.

## Referências primárias

- [ngspice: paralelismo por partições e callbacks](https://ngspice.sourceforge.io/parallel.html).
- [ngspice: XSPICE e algoritmo digital por eventos](https://ngspice.sourceforge.io/xspice.html).
- [ngspice: limites de OpenMP e CUSPICE](https://ngspice.sourceforge.io/cuspice.html).
- [Accellera TLM-2.0: desacoplamento temporal, seção 9.2](https://www.accellera.org/images/downloads/standards/systemc/TLM_2_0_LRM.pdf).
- [Renode: domínios de tempo, quanta e sincronização](https://renode.readthedocs.io/en/latest/advanced/time_framework.html).
