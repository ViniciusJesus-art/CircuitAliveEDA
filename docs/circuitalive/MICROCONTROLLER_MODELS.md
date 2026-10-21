# Modelos programáveis: microcontroladores, SoCs e FPGA

O editor executa firmware HEX/BIN por motores JavaScript externos, usando um
interpretador QuickJS-NG incorporado ao worker de simulação. O protocolo é
independente da arquitetura. Cada instância possui CPU, memória, firmware, clock
e pinagem próprios. Editar o módulo ou trocar o firmware passa a valer no próximo
Play, sem recompilar o aplicativo.

## Implementação disponível

| Motor | Dispositivos com descritores fornecidos |
|---|---|
| AVR8js 0.21.1 | ATmega48P, ATmega88P, ATmega168P, ATmega328P e Arduino UNO R3; encapsulamentos existentes na biblioteca local. |
| PIC16 interpretado | PIC16F84A, encapsulamentos P, SO e SS existentes na biblioteca local. |

A CPU executa as instruções do arquivo escolhido. As entradas vêm das tensões do
circuito; as saídas acionam drivers com resistência finita ligados à alimentação
real do modelo. GPIO de entrada fica em alta impedância ou pull-up conforme os
registradores. Reset e alimentação inválida interrompem a CPU e liberam as saídas.

Os modelos existentes de blocos e ngspice permanecem no schema 1. Modelos com
firmware usam schema 2 e backend `ngspice+runtime`. O editor visual de blocos não
edita a implementação da CPU; o descritor `.casim` e o módulo `.js` são arquivos
separados. A associação ao símbolo e os arquivos de firmware usam a configuração
normal do componente no esquema.

- [Como configurar firmware, pinagem e novos modelos](PROGRAMMABLE_MODEL_FILES.md).
- [Contrato JavaScript e capacidades de cada motor](../../simulation/runtime/README.md).

O instalador Windows e os pacotes Linux incluem interpretador, motores, modelos e
licenças. Não é necessário instalar Node.js, Python, emuladores ou compiladores
para executar a simulação. Compilar um novo firmware continua sendo tarefa da IDE
ou toolchain habitual do microcontrolador; também é possível usar HEX/BIN já pronto.

## Ampliação para outras famílias

Símbolos da biblioteca fornecem desenho e pinos; não implementam a CPU.
STM, ESP e FPGA ainda não têm motores integrados. Outros chips AVR e PIC também
precisam de modelos e capacidades explicitamente implementados e validados.

Um novo módulo pode implementar `createDevice(config)` e ser referenciado por um
`.casim`, usando a mesma ponte elétrica. O PIC16F84A demonstra uma segunda ISA
independente do AVR. Trocar apenas o nome do dispositivo ou sua pinagem não
acrescenta instruções, registradores e periféricos.

Para famílias complexas, futuras integrações podem usar motores como Renode ou
QEMU para dispositivos ARM/SoCs suportados e motores específicos da Espressif
para ESP. Integrações nativas/processos externos ainda precisam de implementação
e distribuição por plataforma; não fazem parte da API JavaScript atual.

FPGA exige um motor HDL/netlist ou uma implementação da configuração da família
exata. Um bitstream proprietário não equivale a um firmware de CPU e não pode ser
tratado como HEX/BIN de um MCU.

## Sincronização e limites

O ngspice confirma um ponto elétrico; o motor executa a próxima janela mantendo
as entradas amostradas. Os eventos de saída preservam os timestamps do firmware
e introduzem breakpoints no solver. Passos analógicos rejeitados leem o histórico
imutável e não executam a CPU novamente. Na retomada, o estado da CPU é preservado;
Stop/Play reinicia as imagens congeladas.

O período padrão de amostragem é 1 µs e pode ser configurado no componente.
O AVR possui um acelerador de instruções nativo incorporado, com a mesma semântica
do AVR8js, e temporizadores agendados por comparação/overflow. Com um único micro e
alimentação/reset constantes, a execução avança entre eventos e entrega o controle
ao solver antes de ler um GPIO ou iniciar uma operação dependente de entradas.
Interrupções externas, recepção UART, ADC ativo e clocks externos retomam a
amostragem configurada. Alimentação variável e múltiplos micros usam a sincronização
conservadora. Os drivers GPIO são condutâncias finitas em um modelo XSPICE incluído
no aplicativo, sem fontes auxiliares por pino.

O cálculo cooperativo do micro usa uma thread própria e uma fila limitada a uma
janela, enquanto o ngspice resolve o circuito a partir dos eventos dos pinos.
A thread para antes de observar entradas novas; operações de pausa e alteração
dos controles permanecem nos limites confirmados do circuito. O editor segue
um relógio absoluto para incluir o processamento no intervalo das atualizações.

Fontes comportamentais compatíveis são avaliadas por um grafo compilado, com
derivadas analíticas. Fontes alteráveis durante a execução e expressões fora do
conjunto suportado continuam usando a avaliação do ngspice. O passo elétrico
permanece adaptativo: fontes periódicas limitam a amostragem, redes RC de timing
do 555 limitam o passo conforme seus valores, e o integrador pode reduzi-lo.
Limites menores configurados pelo usuário e os instantes dos eventos são preservados.

Em 1×, a aplicação acompanha o relógio real quando o cálculo do circuito cabe no
tempo disponível. O clock do firmware e seus intervalos não são acelerados para
compensar um circuito que exija mais processamento que a máquina consegue fornecer.
Entradas e realimentações têm essa latência de amostragem; instruções são atômicas.
Isso não representa sincronização por transistor ou periféricos completos de
todos os dispositivos. As limitações de cada motor estão documentadas e operações
sem suporte geram erro explícito.
