# Transistores, MOSFETs, Zeners e SCR

Foram acrescentados **15 modelos** em arquivos independentes de
`simulation/models/`. Os símbolos continuam vindo da biblioteca padrão do KiCad;
a associação usa o ID de biblioteca declarado no próprio `.casim`.
O 1N4148 já existia e foi verificado novamente, sem duplicar seu modelo.

| Símbolo da biblioteca | Arquivo | Pinagem física |
|---|---|---|
| `Transistor_BJT:TIP122` | `darlington_tip122.casim` | 1=B, 2=C, 3=E |
| `Transistor_BJT:TIP127` | `darlington_tip127.casim` | 1=B, 2=C, 3=E |
| `Transistor_BJT:TIP41C` | `bjt_tip41c.casim` | 1=B, 2=C, 3=E |
| `Transistor_BJT:TIP42C` | `bjt_tip42c.casim` | 1=B, 2=C, 3=E |
| `Transistor_BJT:BD139` | `bjt_bd139.casim` | 1=E, 2=C, 3=B |
| `Transistor_BJT:BD140` | `bjt_bd140.casim` | 1=E, 2=C, 3=B |
| `Transistor_BJT:PN2222A` | `bjt_pn2222a.casim` | 1=E, 2=B, 3=C |
| `Transistor_BJT:2N3904` | `bjt_2n3904.casim` | 1=E, 2=B, 3=C |
| `Transistor_BJT:2N3906` | `bjt_2n3906.casim` | 1=E, 2=B, 3=C |
| `Transistor_FET:2N7000` | `mosfet_2n7000.casim` | 1=S, 2=G, 3=D |
| `Transistor_FET:BS170` | `mosfet_bs170.casim` | 1=D, 2=G, 3=S |
| `Transistor_FET:IRF540N` | `mosfet_irf540n.casim` | 1=G, 2=D, 3=S |
| `Diode:BZV55C5V1` | `zener_bzv55c5v1.casim` | 1=K, 2=A |
| `Diode:BZV55C12` | `zener_bzv55c12.casim` | 1=K, 2=A |
| `Triac_Thyristor:TIC106` | `scr_tic106.casim` | 1=K, 2=A, 3=G |

O nome disponível nesta biblioteca para o transistor plástico da família
2N2222A é **PN2222A**. Não associe automaticamente o mesmo mapa a encapsulamentos
com outra pinagem, como certas versões P2N2222A. A pinagem da biblioteca foi
verificada separadamente das características elétricas do datasheet.

## Uso

Coloque o símbolo padrão e conecte o circuito. **Simulation model...** permite
ajustar os parâmetros de cada instância; a escolha automática funciona quando o
símbolo não tem uma associação explícita anterior. Um perfil pessoal salvo para
aquele componente também conserva sua prioridade.

Os arquivos novos são lidos em tempo de execução. Para atualizar uma sessão,
use **Stop → Play**. Não é necessário recompilar para modificar os modelos.
Eles também aparecem como blocos no **Simulation Model Editor**.

## Representação elétrica e custo

- **Bipolares:** um transistor Gummel-Poon com ganho, redução de ganho em corrente
  alta, efeito Early, resistências internas, capacitâncias e tempos de trânsito.
- **TIP122/TIP127:** dois transistores em Darlington e os resistores B-E internos.
  A entrada conserva as duas junções e a saída apresenta queda de saturação.
  Os resistores internos seguem o [datasheet da ST](https://www.st.com/resource/en/datasheet/tip122.pdf).
- **MOSFETs:** um elemento VDMOS nativo com resistência dependente da tensão de
  gate, capacitância Miller não linear, diodo de corpo e avalanche. O limiar VGS
  não é a tensão necessária para obter a resistência de condução especificada.
  O [IRF540N](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf540n-datasheet-en.pdf)
  foi ajustado para a caracterização com gate a 10 V.
- **Zeners:** caminhos separados para condução direta e regulação reversa,
  calibrados na corrente de teste. Isso permite ajustar a resistência dinâmica
  reversa sem elevar artificialmente a queda direta. Referência:
  [BZV55, Nexperia](https://assets.nexperia.com/documents/data-sheet/BZV55_SER.pdf).
- **TIC106:** mede a corrente real de gate e usa um estado interno para conservar
  a condução após o pulso. Desliga abaixo da corrente de manutenção ou com
  polarização reversa. Os padrões são IGT = 200 µA e IH = 5 mA, com aproximação
  da queda de 1,7 V a 5 A, conforme os pontos do
  [datasheet da série TIC106](https://www.farnell.com/datasheets/56920.pdf).

Os modelos BJT/Darlington novos solicitam **Gear de ordem até 2** por uma linha
`.options method=gear maxord=2`. A preferência vale para o circuito que os contém,
pois o integrador do ngspice é compartilhado. Ela elimina a oscilação numérica
observada no desligamento com armazenamento de carga, inclusive ao alterar uma
fonte durante Pause. Não impõe um passo global de nanossegundos. A escolha do
modelo VDMOS e as opções de integração seguem o
[manual do ngspice](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf).

Todos usam um número pequeno e fixo de elementos. Somente os sinais solicitados
pelo editor são armazenados; não há execução por componente no timer da interface.

## Precisão e limites

Os parâmetros são aproximações próprias, em temperatura ambiente, ajustadas a
pontos e faixas dos datasheets. Os arquivos registram as fontes em `sources` e
as simplificações em `model_notes`. Não são modelos certificados dos fabricantes.
O ganho, a corrente de disparo e outras características reais variam entre lotes.

Não há autoaquecimento, destruição, área segura de operação ou reprodução completa
da dependência com temperatura. Nos BJTs e Darlingtons não se modela ruptura
destrutiva. O TIC106 não reproduz disparo por dv/dt nem toda a recuperação de carga;
o tempo de resposta é uma aproximação configurável. O símbolo TIC106 abrange
sufixos com diferentes tensões máximas, que não são simuladas como falhas.

Os limites de corrente configurados geram avisos; não são limitadores artificiais
de corrente. Nos Zeners, o aviso usa aproximadamente 400 mW/VZ e não substitui
uma análise térmica ou de pulsos.

## Pontos verificados

| Modelo | Condição do ensaio | Resultado aproximado |
|---|---|---|
| TIP122 / TIP127 | 12 V, carga 3,3 Ω, base por 283 Ω e diferença de acionamento de 5 V | IC = 3,36 A; \|VCE\| = 0,92 V |
| IRF540N | VGS = 10 V, ID = 16 A | RDS = 41,4 mΩ |
| 2N7000 | VGS = 10 V, ID = 75 mA | RDS = 3,06 Ω |
| BS170 | VGS = 10 V, ID = 200 mA | RDS = 1,61 Ω |
| BZV55C5V1 | IZ = 5 mA | VZ = 5,100 V; resistência dinâmica ≈ 39,5 Ω |
| BZV55C12 | IZ = 5 mA | VZ = 12,000 V; resistência dinâmica ≈ 9,46 Ω |
| TIC106 | IT = 5 A, gate acionado | VAK ≈ 1,70 V |
| 1N4148 existente | IF = 10 mA / 100 mA | VF ≈ 0,691 V / 0,843 V |

Também foram testados ganho em região ativa, desligamento por pulsos, acionamento
interativo abrupto, diodo de corpo dos MOSFETs, retenção do SCR após o pulso,
desligamento abaixo de IH e bloqueio reverso. Os pontos são verificações locais,
não uma validação de todas as curvas dos fabricantes.

As outras referências são os datasheets
[BD139/BD140](https://www.st.com/resource/en/datasheet/bd139.pdf),
[TIP41C/TIP42C](https://www.st.com/resource/en/datasheet/tip41c.pdf),
[PN2222A, características elétricas da família P2N2222A](https://www.onsemi.com/download/data-sheet/pdf/p2n2222a-d.pdf),
[2N3904](https://www.onsemi.com/download/data-sheet/pdf/2n3904-d.pdf),
[2N3906](https://www.onsemi.com/download/data-sheet/pdf/2n3906-d.pdf),
[2N7000](https://www.onsemi.com/download/data-sheet/pdf/2n7000-d.pdf),
[BS170](https://www.onsemi.com/download/data-sheet/pdf/bs170-d.pdf) e
[1N4148](https://www.vishay.com/docs/81857/1n4148.pdf).

## Exemplo e validação

```bash
./circuitalive eeschema demos/simulation/circuitalive/common_discrete_models.kicad_sch
tools/circuitalive/test-interactive.sh --verbose
tools/circuitalive/test-spice.sh --verbose
```

O exemplo contém TIP122, TIP127, IRF540N, Zener e TIC106, todos com associação
automática, medições V/I/P e probes do pulso de gate e do anodo do SCR.
O pulso de 20 µs começa em 1 ms; o SCR continua ligado após ele.

A ferramenta `tools/circuitalive/validate-model-pins.py` compara as associações
dos arquivos com os números e nomes de terminais da biblioteca instalada:

```bash
python3 tools/circuitalive/validate-model-pins.py simulation/models/darlington_tip122.casim
```

A validação completa desta etapa teve **89 testes interativos e 68 nativos**.
O benchmark local com 100 componentes, misturando os 15 modelos, simulou 250 ms
com passo máximo de 100 µs em 0,24–0,28 s, com pico de 18,6–19,2 MiB no processo
ngspice. Foram 2595 pontos aceitos e 100 vetores salvos. É uma bancada simples
com fontes DC e um pulso, executada sem a interface; circuitos com frequências
altas, carga indutiva e muitas transições têm outro custo. O resultado está em
`.circuitalive/logs/discrete-model-benchmark.json`.
