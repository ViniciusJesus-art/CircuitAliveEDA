# Incorporar componentes à instalação

Uma biblioteca de projeto usa `${KIPRJMOD}` e só se aplica àquele projeto.
Uma biblioteca global é consultada em todos os projetos. Registrar nela um caminho
`${KIPRJMOD}/Wall8.pretty/...` faz o programa procurar a mesma subpasta em todo
projeto novo. O vínculo incorreto pode existir mesmo sem inserir nenhum símbolo.

O CircuitAliveEDA mantém seus componentes adicionais em
`resources/component-libraries/`, dentro do repositório, separados das bibliotecas
padrão e das configurações locais. Essa pasta entra no EXE, RPM, DEB e AppImage
quando eles forem gerados novamente. Instaladores já gerados não são alterados.

Para incorporar todas as bibliotecas nativas das tabelas de um projeto:

```bash
./circuitalive library add "/caminho/do/projeto" --watch
```

Também é possível incorporar uma biblioteca isolada:

```bash
./circuitalive library add "/caminho/MinhaLib.pretty"
./circuitalive library add "/caminho/MinhaLib.kicad_sym"
```

Importe primeiro os footprints se o símbolo isolado os referenciar. `--name Nome`
define o nome global de uma biblioteca isolada. Os nomes das bibliotecas das tabelas
do projeto são preservados. Use nomes distintos para bibliotecas diferentes.

O importador copia somente símbolos, footprints e modelos 3D referenciados. Se um
modelo mudou de nome, use `--model-map "nome-antigo.stp=arquivo-existente.STEP"`.
Um componente com dependências ausentes é recusado antes de alterar a coleção.

O modo `--watch` copia adições nas bibliotecas daquele projeto ao iniciar pelo
`./circuitalive`, sincronizar ou empacotar nesta máquina. Novas bibliotecas precisam
estar registradas nas tabelas do projeto. Não há atualização contínua enquanto o
editor está aberto; para sincronizar manualmente, execute:

```bash
./circuitalive library sync
```

Depois reabra o editor para recarregar as bibliotecas. O comando de adição também
instala a coleção no perfil de desenvolvimento. Os arquivos do projeto original
permanecem intactos. Exclusões na origem não removem componentes já incorporados.
Se a origem estiver desconectada, a última cópia continua disponível.

A lista de origens acompanhadas fica em `.circuitalive/component-sources.json`,
apenas nesta máquina. As cópias em `resources/component-libraries/` devem entrar
no controle de versão para outros computadores e builds de CI receberem os
componentes, sem precisar dos projetos originais. Para bibliotecas acompanhadas,
edite a origem cadastrada: a próxima sincronização copia novamente essa origem.
Alterações feitas diretamente na cópia instalada são detectadas e preservadas;
nesse caso a sincronização avisa sobre o conflito em vez de sobrescrevê-las.

Os símbolos e footprints incorporados aparecem globalmente depois de reiniciar
o editor. Bibliotecas locais do projeto com o mesmo apelido continuam prevalecendo
naquele projeto. Para usar explicitamente os componentes distribuídos, escolha
as bibliotecas globais em projetos que não tenham uma biblioteca local homônima.
