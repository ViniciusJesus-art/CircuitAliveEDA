# Bloqueio reverso dos diodos e medição sem carga

Um terminal conectado apenas a uma sonda de tensão pode ficar próximo da tensão
do outro terminal, mesmo quando o diodo impede a passagem de corrente significativa.
As sondas de tensão do CircuitAlive são ideais: medem em relação à referência da
simulação e não acrescentam uma resistência de entrada de multímetro.

## Reprodução com o 1N4148

Catodo K (pino 1, lado da barra) ligado ao símbolo `+5V`; anodo A (pino 2)
ligado à sonda `A`. A bancada nativa produz:

| Conexão do anodo | Tensão em A, relativa ao GND | Corrente do diodo, sentido A → K |
|---|---:|---:|
| Apenas a sonda de tensão | 4,999883 V | Sem corrente de carga |
| Sonda e resistor de 1 kΩ entre A e GND | 3,862 µV | −3,862 nA |

No primeiro circuito, **não há 5 V de polarização reversa entre os terminais**:
ambos estão próximos de +5 V. No segundo, A tem um caminho de retorno ao GND,
K permanece em +5 V e o diodo sustenta praticamente toda a tensão reversa.
O sinal negativo da corrente indica a pequena fuga no sentido K → A.

Os modelos SPICE incluem fuga e capacitância de junção. Além disso, o deck usa
`.options rshunt=1e12`, uma resistência numérica de 1 TΩ por nó para ajudar a
resolver redes sem caminho DC suficiente. O ngspice também usa uma condutância
mínima de junção. Em um nó sem carga, correntes extremamente pequenas podem
definir uma tensão alta; a tensão isolada não demonstra uma passagem de corrente
de carga. O [manual do ngspice](https://ngspice.sourceforge.io/docs/ngspice-manual.pdf),
seções 7.1 e 11.1.2, descreve essas equações e opções.

O [datasheet da Vishay para o 1N4148](https://www.vishay.com/docs/81857/1n4148.pdf)
também especifica corrente reversa finita. Os números da tabela acima são
resultados do modelo distribuído, não medições de uma peça física.

## Como verificar no editor

1. Ligue K ao `+5V`.
2. Ligue A a um resistor de **1 kΩ para GND**.
3. Meça A com uma sonda de tensão. Para o 1N4148 padrão, a leitura fica próxima
   de zero, com a pequena tensão resultante da fuga através do resistor.
4. Ative a medição de corrente do diodo em **Simulation model…**. A corrente
   reversa deve ser pequena. Inverter o diodo nessa bancada alimenta a carga,
   descontando a queda direta da junção.

Schottky têm fuga maior que os modelos PN de silício. Zener passam a conduzir
reversamente na região de sua tensão Zener; os demais diodos também podem entrar
em ruptura se a tensão reversa ultrapassar o valor configurado. A simulação de
ruptura não representa dano térmico ou destruição permanente.

## Cobertura da verificação

Os testes de `qa/tests/spice/test_diode_reverse_bias.cpp` reproduzem o circuito
sem carga e com carga usando as sondas e o símbolo de alimentação reais da
simulação. Também verificam:

- As 31 associações de diodos de dois terminais: genéricos, desenhos alternativos,
  `Simulation_SPICE:D`, 1N4148/W, 1N4001–4007, 1N5400–5408, 1N5817–5819 e
  1N5820–5822, com corrente direta, fuga reversa e ruptura configurada.
- Os 74 símbolos BZV55B/C de 2,4 a 75 V, com carga no anodo e polarização
  reversa abaixo da região Zener.

Os testes existentes de conjuntos BAT54/BAV70/BAV99 e pontes verificam cada
junção nos dois sentidos. Os testes BZV55 existentes verificam regulação reversa
e condução direta. O validador de pinos confere as 141 associações dessas
famílias contra os números e nomes dos pinos da biblioteca padrão.

Essa cobertura verifica os comportamentos citados nas bancadas; não estabelece
uma validação completa de recuperação reversa, frequência ou todos os limites
de cada fabricante. Não foi necessário alterar os modelos para forçar zero
em terminais sem carga.
