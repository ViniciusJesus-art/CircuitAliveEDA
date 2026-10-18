# Revisão de portabilidade e distribuição — 10.0.7-alpha.1

A versão base permanece 10.0.7. O sufixo alpha.1 identifica a primeira distribuição experimental do CircuitAliveEDA; formatos de esquemas, placas, bibliotecas e configuração continuam compatíveis com a base 10.0.

## Problemas encontrados e correções

| Área | Problema | Correção |
|---|---|---|
| Compilação | A simulação interativa era compilada apenas no Linux. | Habilitação dos alvos Windows e Linux, incluindo interface, modelos, worker e testes. |
| Transporte | `socketpair`, `poll`, `posix_spawn`, sinais e descritores POSIX não existem no Windows nativo. | Named pipe local com I/O assíncrona e tempo limite, `CreateProcessW`, herança limitada de handles e Job Object para encerrar o worker com o editor. O transporte POSIX é preservado no Linux. |
| Entrada/saída | A conversão de finais de linha e o compartilhamento de handles poderiam corromper o protocolo no Windows. | Modo binário, handles distintos para stdin/stdout e separação entre mensagens do solver e o canal JSON. |
| Arquivos e processos | Identificação do executável, PID e memória residente dependiam de `/proc` e `unistd`. | API de plataforma com implementações Linux e Windows. Descoberta do worker considera `.exe`. |
| Perfis | `flock` e renomear sobre arquivo existente não são portáveis para Windows. | `LockFileEx`, `MoveFileExW` e diretório de perfis em AppData; isolamento da configuração CircuitAliveEDA. |
| Memória simulada | O módulo dependia de símbolos internos exportados pela biblioteca Linux do ngspice. | Interface oficial de callbacks XSPICE, wrappers do módulo, suporte a strings dinâmicas e exportação explícita do backend no Windows. ABI ngspice 47; CIDER/KLU configurados conforme a biblioteca usada. |
| Windows | `ERROR`, definido pelos cabeçalhos Win32, colidia com o estado de erro do simulador. | Remoção da macro nos arquivos afetados, inclusive no teste nativo. |
| ABI Windows | A biblioteca MSYS2 habilita OSDI, que altera o layout de `IFdevice`. | Configuração correspondente no módulo de memória, evitando deslocamento dos ponteiros de funções. |
| Histórico em tempo real | A estimativa de memória do ngspice no Windows assume armazenamento de todos os passos e rejeitava circuitos que usam resolução de nanossegundos. | `no_mem_check` no worker isolado: o modo `.save none` mantém um ponto no solver e um buffer com limites próprios, verificados pela regressão. |
| Caminhos do ngspice | Aspas duplas no comando de carregamento e caminhos POSIX no ambiente Windows impediam carregar os módulos. | Caminhos relativos ao executável no Windows, caminhos nativos no ambiente e aspas simples para caminhos Linux com espaços. |
| Modelos e idioma | Conversões de texto poderiam depender da página de código; limites numéricos formatados em PT-BR poderiam usar vírgula, rejeitada pelo formato SI do modelo. | UTF-8 explícito nas interfaces novas e formatação numérica independente do idioma para parâmetros salvos. IDs, equações e valores internos permanecem estáveis. |
| Bibliotecas Windows | Copiar dependências da instalação MSYS2 poderia sobrescrever DLLs e bindings recém-compilados com versões da base. | Cópia apenas das dependências ausentes, preservando os binários CircuitAliveEDA. |
| Python Linux | O runtime Fedora procura a biblioteca padrão em `lib64`. | Biblioteca padrão, extensões e wxPython incluídos, com o layout `lib`/`lib64` preservado. |
| Relocação Linux | As tabelas globais e traduções ainda poderiam procurar o prefixo usado na compilação. | Launcher define `APPDIR`, `KICAD_STOCK_DATA_HOME` e diretórios das bibliotecas/modelos usando a localização da instalação. |
| Distribuição | O checkout dependia de RPMs e diretórios locais, sem instaladores. | Empacotadores RPM/portátil Linux e NSIS Windows com bibliotecas de componentes, runtime, traduções, licenças e checksums. Históricos locais e metadados Git são excluídos. |

## Conteúdo Linux

- 223 bibliotecas de símbolos, 15.464 footprints e 7.252 modelos STEP.
- 188 arquivos de modelos/blocos de simulação.
- Editores, gerenciador, ferramentas auxiliares, traduções e runtime Python/wxPython.
- ngspice e módulos XSPICE correspondentes, incluindo memória interativa.
- Licenças da aplicação, das bibliotecas de componentes e das dependências incluídas.

## Validação

A compilação completa Linux passou. Foram executados 177 testes de simulação e 77 testes de integração do editor/SPICE, todos aprovados; três testes adicionais dessa suíte estão desativados por padrão. A verificação específica do módulo de memória também passou após a última adaptação.

O pipeline Windows compila primeiro o simulador e sua suíte de regressão e, após aprovação, compila os demais alvos, monta o instalador e exercita a instalação silenciosa e o executável instalado. O resultado da execução correspondente deve ser consultado no GitHub Actions antes de publicar o artefato.

Os testes dos pacotes usam perfis temporários; a configuração pessoal do usuário não faz parte dos instaladores. O pacote Linux é conferido sem os caminhos de bibliotecas/Python do ambiente de desenvolvimento.

## Plataformas e limites da alpha

- Linux: x86_64, Fedora 44, glibc 2.43 ou compatível. O pacote portátil não promete compatibilidade com distribuições de glibc mais antiga. OpenGL e drivers de vídeo permanecem a cargo do sistema.
- Windows: build nativo x86_64 com MSYS2/UCRT64, destinado a Windows 10/11. O usuário final não precisa instalar MSYS2, compiladores ou KiCad separadamente.
- A marca alpha é mantida até ampliar a validação de hardware gráfico e uso prolongado em máquinas independentes. Os testes automatizados não substituem essa validação.

As bibliotecas e recursos do projeto-base preservam seus formatos e licenças. A origem do projeto permanece creditada no diálogo Sobre.
