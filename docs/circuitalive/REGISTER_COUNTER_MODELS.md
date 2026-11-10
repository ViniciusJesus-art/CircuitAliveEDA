# Registradores, decodificadores e contadores

Este lote acrescenta **35 arquivos `.casim` e 42 associações automáticas**
com pinagem compatível. O catálogo passa de 229 arquivos / 342 associações
para **264 arquivos / 384 associações**. Restam 20.645 entradas eletrônicas
sem associação na biblioteca local de 21.029 entradas; conectores e itens
não eletrônicos continuam excluídos.

Todos os comportamentos específicos estão em `simulation/models/`. Os modelos
usam portas, flip-flops D e latches SR do backend existente, sem acrescentar
uma lista de componentes ao editor ou executar scripts a cada atualização.

## Componentes

| Grupo | Símbolos da biblioteca padrão | Arquivos | Associações |
|---|---|---:|---:|
| Serial → paralelo | 74HC164, 74HCT164 | 2 | 2 |
| Paralelo → serial | 74HC165, 74LS165 | 2 | 2 |
| Shift register com armazenamento de saída | 74HC595, 74HCT595, 74AHC595, 74AHCT595, 74LS595 | 5 | 6 |
| Registrador paralelo com clear | 74HC273, 74HCT273, 74AHC273, 74AHCT273, 74LS273, 74LS174, 74LS175 | 7 | 7 |
| Latch transparente | 74HC373, 74HCT373, 74AHC373, 74AHCT373, 74LS373 | 5 | 5 |
| Registrador paralelo com saída tri-state | 74HC374, 74HCT374, 74AHC374, 74AHCT374, 74LS374 | 5 | 5 |
| Decodificadores | 74HC138, 74HCT138, 74LS138, 74LS139 | 4 | 6 |
| Contadores síncronos | 74LS161, 74LS163 | 2 | 4 |
| Contadores ripple | 74LS393, CD4020B, CD4040B | 3 | 5 |

As associações incluem desenhos IEEE explicitamente conferidos, quando o
mesmo componente e a mesma pinagem estão presentes. Famílias HC, HCT, AHC,
AHCT e LS têm arquivos próprios, com diferenças nos limiares, alimentação,
atrasos e estágio de saída. Não se associam nomes de outra família para
aumentar artificialmente a cobertura.

## Funcionamento dos registradores

- **164:** DSA e DSB entram por uma porta AND; cada subida de CP desloca um bit.
  MR baixo limpa os oito estágios, independentemente do clock.
- **165:** PL baixo carrega D0–D7 de forma assíncrona, inclusive quando os dados
  mudam sem clock. CP OR CE forma o clock interno. CE alto impede clocks de CP;
  mudar CE de baixo para alto enquanto CP está baixo pode causar uma captura.
  Mude CE com CP alto. Q7 e sua saída complementar ficam disponíveis.
- **595:** há oito estágios de deslocamento e oito de armazenamento, com clocks
  separados. MR baixo limpa somente o primeiro banco. OE alto desliga as saídas
  paralelas sem alterar os estados; a saída serial continua ativa. Com os clocks
  ligados juntos, o armazenamento captura a palavra anterior do shift register.
- **273/174/175:** cada saída captura seu próprio D na subida do clock comum;
  clear baixo é assíncrono. O 175 oferece as duas polaridades de cada bit.
- **373:** LE alto deixa os dados atravessarem o latch; LE baixo retém o último
  valor. OE alto põe as saídas em Z; o armazenamento continua funcionando.
- **374:** a captura ocorre somente na subida do clock. OE desliga as saídas
  sem apagar ou impedir atualizações do registrador interno.

O estado inicial é X. Use clear, carregamento paralelo ou clocks suficientes
para inicializar o dispositivo, conforme o circuito físico. O armazenamento
de saída do 595 e os 373/374 não têm reset implícito de energização.

## Decodificação e contagem

O **138** seleciona uma saída ativa em baixo entre oito quando os pinos 4 e 5
estão baixos e o pino 6 está alto. Desabilitado, mantém todas as saídas altas.
O **139** tem dois bancos independentes de quatro saídas ativas em baixo;
cada banco tem endereço e enable próprios. As saídas inativas dirigem HIGH,
não alta impedância.

**161/163** contam na subida do clock quando CEP e CET estão altos. PL baixo
carrega a palavra paralela na próxima subida e tem prioridade sobre os enables.
MR baixo tem prioridade sobre PL: no 161 o clear é assíncrono; no 163 é síncrono.
TC é CET AND Q0 AND Q1 AND Q2 AND Q3, independentemente de CEP. O próximo estado
é composto por lógica combinacional; respeite seu tempo de estabilização antes
do clock, inclusive ao cascatear TC no CET do próximo contador.

O **393** possui dois contadores binários de quatro estágios, com clocks na
borda de descida e resets ativos em alto independentes. O **4020** tem 14
estágios internos, dos quais os bits de índice 1 e 2 não têm pinos externos.
O **4040** tem 12 estágios. Ambos usam borda de descida e reset assíncrono alto.
As etapas ripple mudam em sequência, com atraso por estágio. Não há oscilador
ou rede de temporização implícita.

O desenho `4xxx_IEEE:4040` da biblioteca local tem rótulos Q inconsistentes.
A associação segue os números físicos do datasheet, iguais aos do símbolo
normal: por exemplo, pino 7 é o bit de índice 1 e pino 1 é o bit de índice 11.

## Parâmetros e limites

| Família | Alimentação adotada | VIL / VIH relativos a VCC−GND | Atraso por bloco | Resistência de saída |
|---|---|---|---|---|
| 74HC | 2–6 V | 0,3 / 0,7 | 15 ns | 35 Ω |
| 74HCT | 4,5–5,5 V | 0,16 / 0,4 | 18 ns | 35 Ω |
| 74AHC | 2–5,5 V | 0,3 / 0,7 | 8 ns | 25 Ω |
| 74AHCT | 4,5–5,5 V | 0,16 / 0,4 | 8 ns | 25 Ω |
| 74LS | 4,75–5,25 V | 0,16 / 0,4 | 15 ns | 25 Ω |
| CD4000 | 3–18 V | 0,3 / 0,7 | 100 ns por estágio | 750 Ω |

São parâmetros nominais aproximados próximos de 5 V e temperatura ambiente,
editáveis por instância. LS aproxima níveis sem carga de 3,4/0,2 V; os demais
usam os trilhos. Há capacitância nas entradas e consumo quiescente do pacote.
O consumo LS adotado varia de 8 a 28 mA conforme o CI. Os atrasos são de cada
bloco da composição; não representam toda a matriz de tempos do datasheet.

Os clocks físicos usam a histerese proporcional existente no compilador.
Dados e outros controles mantêm conversão 0/X/1; a ação Schmitt em todas as
entradas dos AHC/AHCT não é reproduzida. Os limiares TTL são calibrados em
0,8/2 V a 5 V e escalam com a alimentação.

Sem alimentação válida os drivers ficam em Z. Não há garantia de retenção
através de um ciclo de alimentação. Setup/hold, metastabilidade, largura mínima
de pulso, fan-out e correntes TTL completas, diodos de proteção, alimentação
parasitária, temperatura e destruição não são simulados. Estes são modelos
funcionais próprios, não macromodelos certificados pelos fabricantes.

Não se impõe um passo global de nanossegundos. Eventos XSPICE representam
atrasos internos; escolha a resolução elétrica apropriada para os estímulos.
As bancadas usam 20 ns para testar clocks e controles com folga em relação
aos atrasos, e pulsos com breakpoints nos testes longos de divisão de clock.

## Fontes e validação

Cada arquivo registra os datasheets oficiais específicos em `sources`.
As referências incluem os documentos Nexperia de
[164](https://assets.nexperia.com/documents/data-sheet/74HC_HCT164.pdf),
[165](https://assets.nexperia.com/documents/data-sheet/74HC_HCT165.pdf),
[595 HC/HCT](https://assets.nexperia.com/documents/data-sheet/74HC_HCT595.pdf),
[595 AHC/AHCT](https://assets.nexperia.com/documents/data-sheet/74AHC_AHCT595.pdf),
[273](https://assets.nexperia.com/documents/data-sheet/74HC_HCT273.pdf),
[373](https://assets.nexperia.com/documents/data-sheet/74HC_HCT373.pdf),
[374](https://assets.nexperia.com/documents/data-sheet/74HC_HCT374.pdf) e
[138](https://assets.nexperia.com/documents/data-sheet/74HC_HCT138.pdf), além de
[161/163 da TI](https://www.ti.com/lit/ds/symlink/sn74ls161a.pdf),
[393 da TI](https://www.ti.com/lit/ds/symlink/sn74ls393.pdf) e
[4020/4040 da TI](https://www.ti.com/lit/ds/symlink/cd4020b.pdf).

As bancadas em `qa/tests/spice/test_register_counter_models.cpp` usam pinagens
e resultados esperados independentes da ordem dos terminais dos `.casim`.
Exercitam todos os bits e bancos físicos, clocks compartilhados, polaridade
das bordas, reset, carregamento, retenção, tri-state, todas as combinações de
endereço/enable dos decodificadores e todos os estágios até o rollover dos
contadores CD4020/4040.

As **13 bancadas novas passaram em 9.354 verificações**. A suíte completa
passou nos **263 testes interativos**, além dos **6 testes da ferramenta de
catálogo**; as **42 associações** novas tiveram a pinagem física validada.
O limite de execução da suíte passou a 180 segundos para acomodar os testes
de períodos completos dos contadores de 12/14 bits.

```bash
tools/circuitalive/build.sh --target qa_interactive_sim
tools/circuitalive/test-interactive.sh
python3 -m unittest discover -s tools/circuitalive/tests -v
python3 tools/circuitalive/model-catalog.py --fail-on-errors
```

Os arquivos podem ser carregados com **Stop → Play**, conservando a prioridade
de uma associação explícita ou perfil pessoal existente.
