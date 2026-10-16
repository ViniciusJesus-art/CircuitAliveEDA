# Alimentação pelos símbolos de potência

Na simulação interativa, um símbolo de alimentação com tensão numérica no campo
**Value** também fornece essa tensão, referenciada ao zero comum `GND`/`GNDREF`.
Não é necessário colocar uma fonte VDC separada ou associar um arquivo de modelo
para esses símbolos.

| Value do símbolo de alimentação | Tensão em relação ao GND |
|---|---|
| `+5V` | +5 V |
| `+24V` | +24 V |
| `-12V` | −12 V |
| `+3V3` ou `+3.3V` | +3,3 V |
| `+1V8` | +1,8 V |
| `500mV` | +0,5 V |

Edite o **Value**, não apenas o texto de descrição do símbolo. Por exemplo,
`power:+12V` com Value alterado para `+24V` fornece 24 V e passa a nomear sua rede
`+24V`. A orientação do desenho não inverte a tensão: o sinal escrito no valor
é que define a polaridade. Um símbolo `power:VCC` cujo Value seja `+5V` também
funciona; `VCC` sem valor numérico continua sendo um nome de rede.

## Conexões e referência

Os nomes e conexões continuam vindo do grafo nativo do KiCad. Duas setas globais
`+5V` em lugares diferentes representam a mesma rede elétrica. O simulador cria
uma fonte única para essa rede, e todas as cargas compartilham a alimentação.
Símbolos de alimentação locais continuam respeitando o escopo das folhas.

`GND`, `GNDREF` e `0` são a referência comum de 0 V da simulação interativa, inclusive
para probes e sinais digitais. Não é preciso acrescentar uma label GND quando o
símbolo já está presente. Esses símbolos são referências, não fontes negativas.
Para uma alimentação negativa, use, por exemplo, uma seta com Value `-12V`.

Nomes como `+5VA` e `+5VD` fornecem 5 V e continuam sendo redes diferentes, a menos
que haja uma conexão no esquema. Outros nomes de terra, como `GNDA`/`GNDD`, não
são unidos automaticamente a `GND` por esta mudança. Conecte o domínio à referência
adequada no circuito. Uma label de texto comum `+5V` sozinha não cria uma fonte;
a função de alimentação é ativada por um símbolo de potência.

Se não houver um símbolo GND desenhado, a alimentação numérica já tem uma referência
interna de 0 V. Acrescentar GND ou GNDREF conecta o retorno desejado a essa referência.

## Fontes existentes e inconsistências

Uma fonte DC explícita que já fixa a mesma tensão na mesma rede é reaproveitada.
O mesmo vale para uma combinação em série de fontes DC que fixa essa tensão e
para caminhos com probes ideais de corrente. Isso evita acrescentar fontes ideais
redundantes em paralelo.

Ligar símbolos `+5V` e `+24V` na mesma rede produz uma mensagem de conflito antes
de iniciar o ngspice. Uma alimentação não nula ligada diretamente ao GND produz
uma mensagem de curto. Uma fonte DC explícita de valor incompatível também é
identificada antes de iniciar a sessão.

As fontes automáticas são ideais DC, como a fonte DC básica: não acrescentam
resistência interna ou limite de corrente. O monitor avisa quando a corrente de
uma fonte automática passa de 10 A; esse aviso não funciona como limitador.
**Exclude from simulation** no símbolo de alimentação impede que ele crie a fonte;
sua função nativa de nomear/interligar redes continua existindo.

## Exemplo e implementação

Abra `demos/simulation/circuitalive/power_symbol_sources.kicad_sch` e pressione
Play. Não há fonte VDC no esquema. As medições mostram +5 V, +24 V, −12 V e +3,3 V;
dois resistores em posições distintas compartilham a rede `+5V`.

O código próprio fica em `eeschema/circuitalive/power_rails.*`. A captura dos
símbolos e suas conexões permanece no adaptador do esquema, e a montagem do deck
reaproveita o solver e as medições existentes. Nenhum símbolo da biblioteca padrão
é alterado. Stop → Play lê novamente o Value e as conexões atuais; editar o esquema
invalida a sessão em curso normalmente.

## Verificação

71 testes em `qa_interactive_sim` e 67 em `qa_spice` aprovados, incluindo valores
com sinal/decimal/unidade, nomes sem tensão, repetição de setas, tensões bipolares,
fontes DC explícitas normais/invertidas/em série, conflitos e curtos, probes de
corrente, capacitor diretamente ligado à alimentação, entradas/CI digital e captura do esquema nativo com Value
editado. Logs locais: `.circuitalive/logs/test-power-session.log` e
`.circuitalive/logs/test-power-native.log`.
