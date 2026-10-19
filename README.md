<div align="center">

# CircuitAliveEDA

<img src="./resources/bitmaps_png/icons/icon_kicad.png" alt="Logo do CircuitAliveEDA" width="160" />

**Desenhar, simular e desenvolver circuitos eletrônicos no mesmo lugar.**

*Projeto pessoal baseado no [KiCad](https://www.kicad.org/).*



---




## O que é o CircuitAliveEDA?

O CircuitAliveEDA é um **ambiente de desenvolvimento eletrônico baseado no KiCad**, com modificações próprias focadas principalmente na simulação interativa.

A intenção é que a simulação não seja uma etapa separada do desenho. Você monta o circuito usando símbolos eletrônicos, inicia a execução e acompanha o comportamento dos componentes diretamente no esquemático. Também pode interagir com determinados elementos, fazer medições e analisar sinais enquanto trabalha.

Por utilizar o KiCad como base, o projeto também mantém o caminho para o desenvolvimento de placas de circuito impresso, incluindo bibliotecas, edição de PCB e geração dos arquivos necessários para fabricação. Quero preservar o máximo possível dessa compatibilidade, tanto com o KiCad quanto com as bibliotecas e ferramentas criadas por sua comunidade.

**O CA ainda está em desenvolvimento.** Algumas funcionalidades já estão implementadas, outras têm suporte inicial e várias fazem parte dos objetivos futuros. Não é uma versão oficial do KiCad nem tem qualquer afiliação com sua equipe.

---

#### **O CircuitAliveEDA é um projeto estritamente pessoal, desenvolvido e moldado de acordo com o que eu gostaria de encontrar em um software de desenvolvimento eletrônico: uma ferramenta intuitiva, prática e, ao mesmo tempo, profissional, que me permita desenvolver meus projetos desde os primeiros testes até a criação da PCB.**

#### **O projeto continuará evoluindo conforme minhas necessidades e ideias. Contudo, se mais alguém se identificar com essa proposta ou encontrar utilidade no programa, fique à vontade para utilizá-lo também!**

</div>



## Sumário

- [O que é o CircuitAliveEDA?](#o-que-é-o-circuitaliveeda)
- [O que já funciona](#o-que-já-funciona)
- [Imagens do projeto](#imagens-do-projeto)
- [O que ainda quero desenvolver](#o-que-ainda-quero-desenvolver)
- [Como funciona por dentro](#como-funciona-por-dentro)
- [Especificações rápidas](#especificações-rápidas)
- [Documentação](#documentação)
- [Compilação e execução](#compilação-e-execução)
- [Créditos e agradecimentos](#créditos-e-agradecimentos)
- [Licenciamento](#licenciamento)

---

## O que já funciona

### Simulação no próprio esquemático

O CA já conta com uma implementação inicial de simulação interativa usando **ngspice**, com suporte inicial a circuitos digitais por meio do **XSPICE**.

Entre as funções que já foram desenvolvidas estão:

- Controles para iniciar, pausar, continuar e interromper a simulação.
- Integração com o editor de esquemáticos.
- Interação com chaves durante a simulação.
- Representação visual do funcionamento de LEDs.
- Medições de tensão, corrente e potência.
- Osciloscópio com cursores, *trigger* e exportação CSV.
- Indicação dos estados lógicos `0`, `1`, `Z` e `X`.
- Testes com circuitos analógicos e digitais.

Quero que testar um circuito seja algo tão natural quanto desenhá-lo: mudar uma ligação, apertar *Play* e ver o resultado.

### Modelos de componentes

Uma parte importante do projeto é facilitar o uso de modelos de simulação. Não adianta ter uma boa interface se cada novo componente exigir uma sequência complicada de configurações.

O CA já possui suporte e documentação para modelos de componentes discretos e digitais, além da associação desses modelos aos símbolos. Também comecei um **editor visual de modelos**, para tornar esse processo mais acessível.

O [catálogo de modelos](docs/circuitalive/MODEL_CATALOG.md) reúne informações sobre os arquivos `.casim`, modelos disponíveis, testes e cobertura da biblioteca padrão.

### Pequenas mudanças no editor

Além do simulador, comecei a adaptar algumas interações do editor para deixar o trabalho mais rápido. Entre elas estão ajustes na colocação contínua de símbolos e a possibilidade de **excluir elementos com dois cliques no botão direito**.

São detalhes pequenos, mas que fazem diferença quando se passa bastante tempo montando e modificando circuitos.

## Imagens do projeto

As imagens abaixo mostram o CircuitAliveEDA durante o desenvolvimento:

![CircuitAliveEDA — captura do projeto 1](./resources/screenshot/Screenshot_20261017_163020.png)

![CircuitAliveEDA — captura do projeto 2](./resources/screenshot/Screenshot_20261017_165517.png)

![CircuitAliveEDA — captura do projeto 3](./resources/screenshot/Screenshot_20261017_165613.png)

![CircuitAliveEDA — captura do projeto 4](./resources/screenshot/Screenshot_20261017_165649.png)

---

## O que ainda quero desenvolver

Não quero que o CA fique limitado a circuitos pequenos ou exemplos didáticos. Minha intenção é que ele também sirva para projetos maiores, com centenas ou milhares de elementos, circuitos analógicos e digitais, memórias, processadores e outros sistemas eletrônicos.

Algumas das ideias que quero explorar:

- Ampliar a biblioteca de componentes que podem ser simulados sem configurações complicadas.
- Permitir a criação e reutilização de modelos personalizados de maneira modular.
- Melhorar os instrumentos virtuais e a interação com os circuitos durante a execução.
- Investigar formas de aproveitar melhor os vários núcleos do processador em simulações complexas.
- Manter um fluxo contínuo entre a ideia, os testes no esquemático e a fabricação da PCB.

Esses são **objetivos de desenvolvimento**, não uma lista de funcionalidades já concluídas. Prefiro registrar o caminho que quero seguir e ir documentando os resultados conforme forem aparecendo.

---

## Como funciona por dentro

O CircuitAliveEDA é um projeto derivado do **KiCad 10.0.7**, desenvolvido principalmente em **C++**. A interface e as ferramentas de esquemático e PCB partem da infraestrutura do KiCad, enquanto as funcionalidades de simulação interativa são integradas por módulos próprios.

### Motor de simulação

O projeto utiliza o **ngspice** como motor de simulação de circuitos e o **XSPICE** para funcionalidades de simulação digital. Sobre essa base, foi desenvolvida uma camada de controle para gerenciar sessões interativas e conectar a simulação às ferramentas do editor.

Isso permite trabalhar com controles de execução, acompanhamento de sinais e interação com os componentes, sem depender exclusivamente de gráficos separados do esquemático.

### Modelos e bibliotecas

A proposta é manter os símbolos e bibliotecas do ecossistema KiCad e associá-los aos modelos necessários para simulação. A estrutura de modelos do CA, incluindo arquivos `.casim`, está documentada no repositório.

O objetivo de longo prazo é que novos modelos possam ser adicionados sem exigir alterações extensas no núcleo do programa.

### Especificações rápidas

| Item | Especificação |
|---|---|
| Projeto base | KiCad 10.0.7 |
| Linguagem principal | C++ |
| Motores de simulação | ngspice / XSPICE |
| Edição de esquemático e PCB | Infraestrutura do KiCad, com modificações do CA |
| Modelos de simulação | Associação aos símbolos e arquivos `.casim` |
| Ambiente inicial de desenvolvimento | Fedora Linux |
| Compilação Windows | Documentação para MSVC |
| Situação | Projeto em desenvolvimento |
| Licenciamento | Conforme as licenças do código derivado e das dependências, incluindo GPL |

## Documentação

Para quem quiser entender melhor o funcionamento ou estudar a implementação, mantenho documentos mais técnicos em [`docs/circuitalive/`](docs/circuitalive/).

- [Compilação no Fedora](docs/circuitalive/BUILD_FEDORA.md)
- [Release manual: RPM, AppImage, DEB e EXE — juntos ou individualmente](docs/circuitalive/RELEASE_WORKFLOW.md)
- [Compilação no Windows / MSVC](docs/circuitalive/BUILD_WINDOWS.md)
- [Arquitetura da simulação interativa](docs/circuitalive/INTERACTIVE_SESSION.md)
- [Integração com o editor](docs/circuitalive/EDITOR_SIMULATION.md)
- [Medições e osciloscópio](docs/circuitalive/MEASUREMENTS.md)
- [Modelos analógicos e digitais](docs/circuitalive/BASIC_AND_DIGITAL_MODELS.md)
- [Editor visual de modelos](docs/circuitalive/MODEL_AUTHOR.md)
- [Associação de modelos](docs/circuitalive/MODEL_FILES.md)
- [Catálogo de modelos](docs/circuitalive/MODEL_CATALOG.md)
- [Mapa geral do desenvolvimento](docs/circuitalive/Mapa_KiCad_Simulacao_Interativa.md)

## Compilação e execução

O desenvolvimento começou no Fedora, e os scripts de compilação para esse ambiente estão no repositório. Com as dependências instaladas, o processo básico é:

```bash
tools/circuitalive/configure.sh
tools/circuitalive/build.sh
tools/circuitalive/install.sh
```

Para iniciar a aplicação, conforme a instalação:

```bash
./circuitalive
```

Consulte os guias de [Fedora](docs/circuitalive/BUILD_FEDORA.md) e [Windows/MSVC](docs/circuitalive/BUILD_WINDOWS.md) para os requisitos e instruções completas. Como o projeto ainda está em desenvolvimento, builds e recursos podem apresentar limitações ou falhas.

---

## Créditos e agradecimentos

### KiCad

O CircuitAliveEDA só é possível porque existe o **[KiCad](https://www.kicad.org/)** e porque sua equipe e comunidade construíram e mantêm uma ferramenta tão completa de desenvolvimento eletrônico.

O editor de esquemáticos, o editor de PCB, a estrutura de bibliotecas, os recursos de fabricação e grande parte da infraestrutura do CA vêm do KiCad. **Todo o crédito por esse trabalho original pertence aos desenvolvedores e colaboradores do projeto.**

- [Código-fonte do KiCad](https://gitlab.com/kicad/code/kicad)
- [Documentação de desenvolvimento](https://dev-docs.kicad.org/)

### ngspice e XSPICE

Também agradeço aos desenvolvedores do **[ngspice](https://ngspice.sourceforge.io/)** e do **XSPICE**, que fornecem uma parte essencial da tecnologia de simulação utilizada pelo CircuitAliveEDA.

### CircuitAliveEDA

As adaptações de interface, a integração da simulação interativa e as demais extensões próprias são desenvolvidas como parte deste projeto.

**Desenvolvimento: Vinicius Dias de Jesus — DMEE Technology**

## Licenciamento

Por ser derivado do KiCad, o CircuitAliveEDA deve respeitar as licenças do código original e de suas dependências, incluindo as obrigações aplicáveis da **GNU General Public License (GPL)**. Os avisos de copyright e créditos dos autores originais devem ser preservados.

O CircuitAliveEDA **não é afiliado oficialmente ao projeto KiCad**.

---

<div align="center">

**CircuitAliveEDA — DMEE Technology**

*Um projeto que comecei para os meus próprios circuitos e que quero continuar desenvolvendo e compartilhando.*

</div>
