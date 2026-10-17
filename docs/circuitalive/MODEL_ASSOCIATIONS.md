# Associações automáticas e demos

## Abrir pelo gerenciador

**File → Open Demo Project…** filtra projetos `.kicad_pro` e `.pro`, não
esquemáticos `.kicad_sch` isolados. Os 12 exemplos em
`demos/simulation/circuitalive/` agora incluem o projeto com o mesmo nome do
esquemático. Selecione esse arquivo e abra o Schematic Editor.

## Como o modelo é encontrado

As associações ficam nos próprios `.casim`, sem uma lista paralela no executável:

1. `CA.Model` escolhido explicitamente tem prioridade.
2. `matches.part_names` reconhece **Value** ou **Sim.Name**. Todos os terminais
   são conferidos pelos nomes elétricos do desenho. Nomes contraditórios que
   selecionam modelos diferentes geram erro de ambiguidade.
3. `library_ids` reconhece o ID completo. `part_names` também reconhece o nome
   do símbolo para variantes declaradas, sempre conferindo seus terminais.
4. `symbol_names` reconhece cópias do mesmo símbolo em bibliotecas locais,
   desde que o conjunto completo de números dos pinos seja compatível.
5. Por último, modelos com `device_hints` podem usar `Sim.Device`.

Exemplo reduzido da família 2N2222:

```json
"matches": {
  "library_ids": ["Transistor_BJT:PN2222A"],
  "part_names": ["PN2222A", "2N2222", "2N2222A", "Q2N2222", "MMBT2222A"],
  "pin_names": {"c": ["C"], "b": ["B"], "e": ["E"]}
}
```

Um `Q_NPN_CBE` com Value `2N2222` usa C=1/B=2/E=3. O PN2222A padrão usa
E=1/B=2/C=3. MMBT2222A, PMBT2222A e PZT2222A usam suas respectivas ordens.
Nomes ausentes ou duplicados e quantidade diferente de pinos impedem essa
associação. O mapeamento manual continua disponível. Nomes de peças e pinos
aceitam diferenças de maiúsculas e espaços nas extremidades; a busca do
navegador também inclui os nomes alternativos.

A família reutiliza a aproximação compacta nominal de PN2222A. Diferenças
específicas de ganho, ruptura e limites de encapsulamento não foram adicionadas;
o aviso de corrente de 0,6 A permanece conservador.

Os modelos de BJTs, Darlingtons, MOSFETs, diodos, zeners, SCRs e optoacopladores
declaram os nomes compatíveis. Por exemplo, `rectifier_schlib:D` com Value ou
Sim.Name `1N4148` usa `diode_1n4148.casim` e confere A/K. Uma peça desconhecida
continua precisando de modelo; não se escolhe um transistor qualquer apenas
pelo número de pinos.

## Parâmetros de exemplos antigos

VDC/VSIN/VPULSE copiadas para bibliotecas locais são reconhecidas pelo nome do
símbolo. `legacy_parameters` declara no `.casim` a importação de valores literais:

- VSIN: `dc`, `ampl`, `f`, `td`, `theta`, `phase`.
- VPULSE: `y1`, `y2`, `td`, `tr`, `tf`, `tw`, `per`.
- VDC: `dc`; a componente DC é 0 quando Value representa apenas parâmetros da
  análise AC, como no exemplo antigo de Sallen-Key.

`ac`/`ph` pertencem à análise de pequeno sinal e não alteram o transiente.
`CA.Parameters` tem prioridade. R/C/L mantêm **Value** quando ele já é numérico;
usam `r=`, `c=` ou `l=` como alternativa quando Value não contém um número.
A janela Simulation model apresenta os parâmetros importados.

Expressões como `TIME > 350m ? 8 : 89` continuam exigindo modelo comportamental:
não são convertidas em constantes. Outros macromodelos e IBIS ainda precisam
de integração própria.

## Verificação

`tools/circuitalive/test-demos.sh --audit` testa as 12 demos interativas e
verifica explicitamente `rectifier` e `amplifier-ac`, incluindo os modelos e
parâmetros da fonte. O relatório completo fica em `.circuitalive/logs/demo-audit.tsv`.

`tools/circuitalive/model-catalog.py --fail-on-errors` confere a biblioteca padrão,
incluindo nomes alternativos e a pinagem elétrica. Ela tem 228 entradas associadas
entre as 21.007 entradas eletrônicas. Cópias em bibliotecas locais e associações
por Value/Sim.Name não entram nessa contagem.
