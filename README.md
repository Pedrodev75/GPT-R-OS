# GPT-R OS v0.7.2 — Kust Edition

Sistema operacional experimental x86-64 escrito em C e Assembly. Inicia via BIOS com bootloader próprio e é desenvolvido para testes no QEMU.

## Novidades da v0.7.2

- **Kust 0.1** compila fontes `.KST` dentro do GPT-R OS. Rode `kust build HELLO.KST` para gerar um executável GTRA v1.1. A imagem inclui o compilador, o manual offline e oito exemplos.
- **GTRA v1.1** suporta código, dados inicializados e BSS em imagens multipágina de até 64 KiB. Os aplicativos GTRA v1 continuam usando seu formato original.
- **Serviço de montagem** permite que aplicativos chamem o assembler do kernel pela syscall 15. O Kust envia o Assembly por um buffer em memória e o FAT16 publica o executável após a montagem e a validação.
- **SYS64** reúne ferramentas do sistema em `/SYS64`. O Terminal resolve nomes como `kust` nessa pasta. A instalação e a atualização de ferramentas são explícitas por meio de `sysinstall`.

O sistema usa FAT16 e mantém o desktop VBE de **1024×768**, o Terminal com pipes e redirecionamento, o Editor UTF-8, o Gerenciador de Tarefas, o relógio RTC, as Configurações persistentes, a Lixeira e a Central de Aplicativos GTRP v1.

O desktop recompõe apenas as áreas afetadas quando um app gráfico atualiza a janela. O Explorador evita reler a pasta aberta quando as alterações no FAT16 afetam outra pasta.

## Rodar no QEMU

Abra o PowerShell na pasta que contém `gpt-r-build.img`. A imagem contém o bootloader, o kernel, os aplicativos de exemplo e o Kust instalado no FAT16. Ajuste o caminho do QEMU conforme a sua instalação. Depois do boot, digite `version` no Terminal para ver `GPT-R OS v0.7.2 - Kust Edition`.

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=gpt-r-build.img,format=raw,if=ide,index=0,snapshot=on" -boot c
```

Com `snapshot=on`, as alterações no disco ficam em uma camada temporária e são descartadas ao fechar o QEMU.

## Primeiro aplicativo em Kust

Execute no Terminal a partir de uma pasta gravável, como `/HOME/DOCS`.

```text
cd /HOME/DOCS
kust version | cat
kust check /SYS64/DOCS/KUST/EXAMPLES/HELLO.KST | cat
kust build /SYS64/DOCS/KUST/EXAMPLES/HELLO.KST | cat
appinfo HELLO.APP
run HELLO.APP | cat
```

O executável fica no diretório atual e imprime `Olá, GPT-R OS!`. `| cat` mostra a saída no próprio Terminal. Ao executar diretamente, o aplicativo abre sua janela de saída.

Para escrever sua própria fonte, abra `edit PRIMEIRO.KST`, digite o código abaixo e salve com **Ctrl+S**. Volte ao Terminal, execute `kust build PRIMEIRO.KST | cat` e depois `run PRIMEIRO.APP | cat`.

```kust
app "Primeiro"

func main() {
    let total: i32 = 7 + 5
    term.print("Total: ")
    term.println(total)
}
```

| Comando | Resultado |
|---|---|
| `kust build FONTE.KST [-o SAIDA.APP]` | Compila para GTRA v1.1. A saída padrão usa o nome-base da fonte no diretório atual. |
| `kust check FONTE.KST` | Verifica a sintaxe, os tipos, os escopos e a geração de Assembly em memória. |
| `kust help` e `kust version` | Mostram os comandos e as versões da linguagem e do compilador. |
| `kust docs [CAPITULO [PAG]]` | Mostra o índice ou uma página de um capítulo. Experimente `kust docs START`. |
| `kust docs --all CAPITULO \| cat` | Mostra um capítulo inteiro no Terminal. |

Kust tem `i32`, `bool`, constantes, variáveis mutáveis e imutáveis, operadores aritméticos e lógicos, condicionais, laços, funções e strings literais UTF-8. As APIs cobrem Terminal, encerramento do aplicativo, operações básicas de arquivo, um buffer de memória e desenho e cliques no desktop. O compilador lê fontes de até 32 KiB. O Editor aceita arquivos de até 4 KiB.

Abra o manual e os exemplos pelo Explorador em `/SYS64/DOCS/KUST`. Os exemplos são `HELLO.KST`, `VARIABLE.KST`, `MATH.KST`, `FLOW.KST`, `FUNCS.KST`, `FILES.KST`, `MEMORY.KST` e `GRAPHIC.KST`. Para testar o exemplo gráfico, execute o `.APP` diretamente e clique no quadro. Para editar um exemplo, copie-o para uma pasta gravável. [Referência da linguagem](docs/kust-language.md) · [Manual offline](docs/kust/README.TXT)

## Área de trabalho

| Ação | Como usar |
|---|---|
| Abrir o menu | Clique em **Menu** ou pressione **F1**. No menu, use as setas e **Enter**. |
| Trocar de janela | Clique na janela ou no botão dela na barra de tarefas. **Alt+Tab** e **F2** também alternam entre as janelas abertas. |
| Mover uma janela | Arraste a barra de título. O contorno indica a posição durante o arraste. |
| Minimizar ou restaurar | Clique em **−** na barra de título para minimizar e no botão da barra de tarefas para restaurar. |
| Fechar uma janela | Clique em **x** na barra de título. Se um programa ainda estiver rodando, fechar a janela encerra esse programa. |

O Terminal abre com o sistema. Clique duas vezes em **Home** ou **Apps** no desktop para ir direto à pasta pelo Explorador.

### Explorador e atalhos

Abra **F1 → Arquivos** para ver a raiz do disco. A lista exibe ícones, nomes, tipos e tamanhos em bytes. Use **Novo → Pasta** para criar uma subpasta, **Voltar** para visitar o local anterior e **Subir** para ir à pasta acima. Duplo clique ou **Enter** abre a pasta selecionada, abre `.txt`, `.asm` e `.kst` no Editor, executa `.APP` ou abre a prévia de `.PKG` na Central. O Editor salva no mesmo diretório em que o arquivo foi aberto.

| Pasta | Conteúdo |
|---|---|
| `HOME/DOCS/` | Espaço para documentos de texto. |
| `HOME/FONTES/` | Fontes Assembly. Inclui uma cópia de `ola.asm`. |
| `APPS/` | Programas e pacotes instalados, incluindo `CALC.APP`. |
| `SYS64/` | Ferramentas do sistema, incluindo `KUST.APP` e o manual em `DOCS/KUST/`. |
| `CONFIG/` | Atalhos em `ATALHOS.DAT` e preferências do desktop em `DESKTOP.DAT`. |

Clique com o botão direito em um item do Explorador e escolha **Criar atalho**. Arraste o ícone no desktop para mudar sua posição. Para conferir se a posição persiste depois de fechar o QEMU, use o modo de disco gravável descrito em [Testar a persistência](#testar-a-persistência).

### Gerenciador de tarefas

Abra **F1 → Tarefas** para ver o PID, o estado, o uso de CPU em modo usuário, a memória em KiB e o tempo decorrido dos `.APP`. A memória contabiliza as páginas efetivamente alocadas para código, dados e BSS, pilha, heap e buffers de arquivo. Selecione um processo e clique em **Encerrar**, inclusive se a janela estiver minimizada. O kernel libera a memória do processo e a janela mostra o resultado até você fechá-la.

### Relógio, Configurações e Lixeira

O relógio do desktop usa o RTC. Em **F1 → Configurações**, use **1** para trocar o tema e **2** para alternar entre 12 e 24 horas. As duas preferências são gravadas no FAT16.

No Explorador, selecione um arquivo e pressione **Delete** para enviá-lo à Lixeira. Em **F1 → Lixeira**, selecione o item e clique em **Restaurar** para devolvê-lo ao caminho original. Se esse nome estiver em uso, a Lixeira preserva os dois arquivos.

### Central de Aplicativos e Jogo da Velha

1. Abra `VELHA.PKG` pelo Explorador ou digite `pkg VELHA.PKG` no Terminal.
2. Confira os dados do pacote e clique em **Instalar**.
3. Em **F1 → Central Apps**, selecione **Jogo da Velha** e clique em **Executar**. X começa. Clique em **Reiniciar** para limpar o tabuleiro ou em **Sair** para encerrar o jogo.

Se o Jogo da Velha já estiver instalado, abra-o pela Central. O formato GTRP v1 aceita pacotes de até 256 KiB com executáveis GTRA v1 ou v1.1. A Central registra instalações em `/APPS/<ID>/PKG.DAT`. Ao desinstalar, remove os arquivos do pacote cujo conteúdo permanece igual ao original e preserva arquivos modificados e arquivos do usuário. [Formato e instalação GTRP](docs/bloco5.md)

### Calculadora gráfica

1. Dê duplo clique em **Apps** no desktop e abra `CALC.APP`.
2. Clique em **7**, **+**, **5** e **=**. O visor deve mostrar **12**.
3. Arraste a janela pela barra de título e faça outra conta. **C** limpa o visor.

A Calculadora usa as chamadas gráficas de desenho e eventos de clique. Veja a [interface gráfica do formato .APP](docs/app-format.md#interface-gráfica) e o [fonte da Calculadora](apps/CALC.S).

## Inicialização

Antes do primeiro prompt, o sistema mostra uma linha para cada etapa do boot, incluindo vídeo, IDT, E820, alocador de páginas, heap, timer PIT, teclado e mouse PS/2, ATA PIO e montagem do FAT16.

| Indicador | Significado |
|---|---|
| `[  OK  ]` | A etapa foi concluída ou a checagem passou. |
| `[FAILED]` | A etapa encontrou uma falha, como um disco detectado sem volume FAT16 válido. |
| `[ SKIP ]` | A etapa foi pulada, como a montagem do FAT16 quando a VM está sem disco de dados. |

Se o kernel iniciar mas não conseguir montar o FAT16, o shell ainda abre.

## Shell

Digite no prompt `>` e pressione **Enter**. Use **↑** e **↓** para navegar pelos últimos 16 comandos. Para consultar o uso de um comando, experimente `man edit`.

| Comando | Resultado |
|---|---|
| `help` | Mostra os nomes dos comandos em duas colunas. |
| `man <comando>` | Mostra a sintaxe e a descrição de um comando. |
| `clear` | Limpa o terminal e preserva o cabeçalho. |
| `version` | Mostra a versão e a edição do GPT-R OS. |
| `echo [texto]` | Escreve texto na saída do Terminal ou de um pipe. |
| `uptime` | Mostra os segundos desde o boot. |
| `mem` | Mostra o mapa E820, a RAM utilizável, a RAM mapeada para o alocador e as páginas livres. |
| `memtest` | Aloca uma página de 4 KiB, verifica os padrões `0xAA` e `0x55` e libera a página. |
| `kmalloc <bytes>` | Aloca um bloco do heap e mostra seu endereço em hexadecimal. Informe o tamanho em decimal. |
| `kfree <endereço>` | Libera um bloco ativo alocado pelo shell. Use o endereço exibido por `kmalloc`. |
| `peek <endereço>` | Mostra 16 bytes a partir de um endereço hexadecimal na RAM gerenciada. Exemplo `peek 0x200000`. |
| `reboot` | Reinicia o sistema no QEMU PC. |
| `color <frente> <fundo>` | Muda as cores do terminal e do cabeçalho. Aceita frente de 0 a 15 e fundo de 0 a 7, com valores diferentes. Experimente `color 15 1`. |
| `crash <tipo>` | Aciona um teste de diagnóstico. |
| `diskinfo` | Mostra o modelo e a capacidade do disco ATA primário, além do espaço total e livre para arquivos no FAT16. |
| `sector <LBA>` | Mostra os primeiros 16 bytes de um setor. Informe o LBA em decimal. |
| `ls [pasta]` | Lista nomes, tamanhos e datas de modificação. Sem argumento, usa o diretório atual. |
| `pwd` e `cd <pasta>` | Mostram o diretório atual e mudam para outra pasta. |
| `mkdir <pasta>` | Cria uma subpasta. |
| `cp <origem> <destino>` e `mv <origem> <destino>` | Copiam, movem ou renomeiam arquivos. |
| `cat <arquivo>` | Exibe um arquivo de texto UTF-8 com paginação. |
| `run <arquivo.APP> [argumentos]` | Executa um programa x86-64 em modo usuário e abre uma janela com o nome do executável. |
| `edit <arquivo>` | Abre um `.txt`, `.asm` ou `.kst` no Editor gráfico. |
| `pkg <arquivo.PKG>` | Abre a prévia do pacote na Central de Aplicativos. |
| `asm [-f v1.1] <fonte.asm> <saida.APP>` | Gera GTRA v1 por padrão. Use `-f v1.1` para gerar GTRA v1.1. |
| `appinfo <arquivo.APP>` | Valida e mostra a versão, a entrada e os tamanhos do executável. |
| `sysinstall [-u] <origem.APP> [NOME.APP]` | Instala uma ferramenta no SYS64. Use `-u` para atualizar uma ferramenta existente. |
| `write <arquivo> <texto>` | Cria ou sobrescreve um arquivo de texto UTF-8. Aceita os acentos digitáveis no layout ABNT2. |
| `rm <arquivo>` | Remove um arquivo e libera seus clusters. |
| `demo` | Abre a demonstração interativa de vídeo no modo gráfico. |

Na `demo`, use as **setas** para mover o quadrado. O timer PIT anima a cor enquanto a demonstração está aberta. Pressione **Esc** para voltar ao terminal com o histórico e as cores preservados.

O heap aceita de 1 a 4.064 bytes por alocação. O shell mantém até 32 blocos ativos. Uma página volta ao alocador físico quando todos os seus blocos são liberados.

### Arquivos e paginação

Use `ls` para encontrar `README.txt` e `cat README.txt` para ler o arquivo. Na pausa do `cat`, **Espaço** avança uma página, **Enter** avança uma linha e **Esc** volta ao prompt. Ao chegar ao fim, aparece `FIM DO ARQUIVO - FAT16 OK`.

Os comandos de arquivos aceitam caminhos absolutos, como `/HOME/DOCS/nota.txt`, e caminhos relativos ao diretório atual do shell. O FAT16 usa nomes curtos 8.3. Digite o nome exatamente como aparece no `ls`. Para criar arquivos, use letras sem acento, números, hífen ou sublinhado. Use uma única caixa em cada parte do nome e a extensão `.txt` em minúsculas para novos arquivos de texto.

Use `diskinfo` antes e depois de `write nota.txt Pão com ovo` para ver o espaço livre mudar. Leia o resultado com `cat nota.txt` e use `rm nota.txt` para liberar os clusters do arquivo. O comando `sector 0` inspeciona os primeiros bytes do MBR. Na imagem de exemplo, a partição FAT16 começa no setor 2048.

### Pipes e redirecionamento

Use `|` para enviar a saída de um comando à entrada da próxima etapa e `>` ao final para gravar a saída em um arquivo. Aplicativos iniciados por `run` em um pipe usam a entrada e a saída do Terminal. Por exemplo, este comando executa `ola.APP`, repassa a saída por `cat` e grava o texto em `saida.txt`.

```text
run ola.APP | cat > saida.txt
cat saida.txt
```

Use aspas para preservar espaços e operadores em argumentos, como em `run programa.APP "dois termos" "" "a|b>c"`. Em pipelines, um aplicativo recebe EOF quando o produtor termina e a fila esvazia. Se o leitor encerrar cedo, o Terminal encerra os produtores anteriores do trecho. **Ctrl+C** interrompe o pipeline com o Terminal em foco. [Pipes e redirecionamento](docs/shell-pipes.md) · [Sintaxe de argumentos](docs/gtra-v1.1.md#argumentos-do-terminal)

### Ferramentas do SYS64

Comandos internos têm precedência. Um nome simples como `kust` procura `KUST.APP` em `/SYS64`. Use `run /SYS64/KUST.APP` para executar pelo caminho explícito. Ferramentas herdam o diretório de trabalho do Terminal e seguem as mesmas regras de argumentos e pipes dos demais aplicativos.

O SYS64 permite leitura e execução e protege seu conteúdo contra alterações comuns. Use `sysinstall ORIGEM.APP` para instalar um aplicativo validado e `sysinstall -u ORIGEM.APP` para substituí-lo. Pacotes `.PKG` continuam sendo instalados pela Central em `/APPS`. [Resolução, instalação e proteção do SYS64](docs/sys64.md)

### Executar vários programas

Cada execução de `run` tem sua própria janela, com saída, pilha e contexto de CPU separados. O desktop suporta até quatro janelas de `.APP`. Quando um programa pede entrada, ele recebe as teclas digitadas na sua janela enquanto ela estiver em foco. **Ctrl+C** encerra o programa em foco. Quando um programa sai ou falha, o kernel libera sua memória e mantém a janela aberta para você ler a saída. Feche a janela para liberar espaço para outro programa.

A imagem de exemplo inclui `loop.APP` e `nome.APP` para testar a multitarefa.

1. No Terminal, execute `run loop.APP`.
2. Clique em **Terminal** na barra de tarefas e execute `run nome.APP`.
3. Na janela `nome.APP`, digite seu nome e pressione **Enter**. A saudação e o código de saída aparecem nessa janela.
4. Selecione `loop.APP` e pressione **Ctrl+C**. A mensagem de interrupção aparece na janela do loop.

### Monte um programa em Assembly

A imagem também vem com `ola.APP`, `ola.asm` e `README.txt`. Abra `edit ola.asm` e mude a mensagem na linha com `db`. O editor mostra números de linha, cursor, rolagem e os atalhos no rodapé. Use **Ctrl+S** para salvar e **Esc** para fechar o editor.

```text
asm ola.asm feito.APP
asm -f v1.1 ola.asm feito11.APP
appinfo feito11.APP
run feito11.APP
```

O programa imprime a mensagem na janela `feito11.APP` e sai com código 0. O assembler v1.1 suporta `.text`, `.data`, `.bss`, `resb` e referências RIP-relativas, com até 64 KiB de fonte e 1.024 rótulos. A imagem v1.1 soma até 64 KiB entre código, dados e BSS e requer NX. [Formato legado e ABI de aplicativos](docs/app-format.md) · [GTRA v1.1 e assembler](docs/gtra-v1.1.md) · [Serviço de montagem](docs/assembly-service.md)

O Editor aceita texto UTF-8 de até 4 KiB. **Enter** mantém a indentação, **Tab** avança até a próxima coluna múltipla de quatro e **Shift+Tab** remove um nível inicial de indentação. **Ctrl+Z** desfaz e **Ctrl+Y** refaz até 16 edições. Arquivos `.asm` têm cores para instruções, rótulos e comentários.

Ao salvar, o Editor confere se o arquivo mudou no disco. Um salvamento sem edições preserva os bytes originais. Após editar, as quebras de linha são gravadas em LF. Se houver alterações pendentes ao apertar **Esc**, o Editor oferece salvar, descartar ou continuar.

### Testar a persistência

Use uma cópia da imagem para conservar o arquivo original. Abra a cópia no QEMU sem `snapshot=on`.

```powershell
if (!(Test-Path gpt-r-teste.img)) {
    Copy-Item gpt-r-build.img gpt-r-teste.img
}
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=gpt-r-teste.img,format=raw,if=ide,index=0" -boot c
```

Compile um exemplo Kust em uma pasta gravável, troque o tema nas Configurações e feche o QEMU. Reabra a mesma cópia para conferir a preferência e executar o `.APP` gerado. Para testar a instalação persistente, use `VELHA.PKG` em uma imagem em que o jogo ainda esteja disponível para instalar e reabra a Central após reiniciar.

### Diagnóstico

| Tipo | Resultado esperado |
|---|---|
| `ud` | `ud2` gera `#UD` (vetor 6). |
| `div` | Divisão inteira por zero gera `#DE` (vetor 0). |
| `pf` | Ler `0x40000000` gera `#PF` (vetor 14) e mostra o endereço em `CR2`. |
| `gp` | Ler `0x8000000000000000` gera `#GP` (vetor 13). |
| `int3` | `int3` gera `#BP` (vetor 3). |
| `alloc` | Aloca até receber `NULL`, libera as páginas do teste e devolve o controle ao shell. |
| `heap` | Verifica alinhamento, conteúdo gravado, união de blocos livres e devolução da página. Exibe `PASS` ou `FAIL` e volta ao prompt. |
| `hang` | Desativa as interrupções mascaráveis e entra em loop. Reinicie a VM pelo QEMU para voltar ao prompt. |
| `triple` | Carrega uma IDT vazia e provoca triple fault. Com as opções padrão, o QEMU reinicia a VM. |

Quando um programa em modo usuário sofre uma exceção, sua janela mostra o vetor e o endereço da instrução. Em uma falha de página, ela também mostra o endereço em `CR2`. As exceções do kernel param na tela de diagnóstico. Ela mostra o vetor, o código de erro e os registradores. Para comparar `mem`, inicie o QEMU uma vez com `-m 64` e outra com `-m 128`.

## Estrutura

| Caminho | Responsabilidade |
|---|---|
| `boot/` | Bootloader BIOS, leitura do HD, configuração VBE, coleta E820 e layout da imagem. |
| `kernel/kernel.c` | Inicializa o kernel, mostra o log de boot e chama o shell. |
| `kernel/apps/` | Carregador `.APP`, chamadas ao kernel, escalonador e montador. |
| `kernel/desktop/` | Área de trabalho, janelas, Explorador, Gerenciador de tarefas, Configurações, Lixeira e Central. |
| `kernel/memory/` | Paginação, mapa E820, alocador de páginas físicas e heap. |
| `kernel/interrupts/` | IDT, entradas de IRQ e diagnóstico de exceções. |
| `kernel/drivers/` | Teclado PS/2 ABNT2, mouse PS/2, timer PIT, console gráfico VBE e disco ATA PIO. |
| `kernel/fs/` | Leitura e escrita de arquivos e pastas FAT16. |
| `kernel/shell/` | Prompt, histórico, comandos, editor gráfico e módulos de disco, memória e diagnóstico. |
| `kernel/packages/` | Validação GTRP, instalação, remoção e adaptador FAT16. |
| `apps/` | Código-fonte dos programas de exemplo, da Calculadora e do Jogo da Velha. |
| `apps/kust/` | Compilador Kust nativo, entrada de aplicação e layout GTRA v1.1. |
| `tools/Pack-App.ps1` | Empacotador de executáveis `.APP` para Windows. |
| `tools/pack-pkg.py` | Empacotador de arquivos `.PKG` GTRP v1. |
| `tools/build-kust.py` | Bootstrap de `KUST.APP` com Python e Zig. |
| `tools/pack-image.py` e `tools/kust_bundle.py` | Montagem da imagem de boot e inclusão do compilador, manual e exemplos no FAT16. |
| `tests/` | Testes de host para Kust, GTRA, FAT16 e GTRP. |
| `docs/app-format.md` | Formato `GTRA`, chamadas de texto e vídeo, eventos de mouse, sintaxe do montador e exemplos. |
| `docs/kust/` | Manual e exemplos que acompanham a imagem. |
| `docs/bloco5.md` | Formato `GTRP`, ciclo de instalação e recuperação. |

## Gerar uma imagem

Compile e ligue o kernel com `boot/layout.ld` e extraia `boot.bin`, `loader.bin` e `kernel.bin`. Gere o compilador no host com Python 3 e Zig 0.15.2. O empacotador usa uma imagem-base FAT16 já particionada e coloca o Kust e o manual em `/SYS64` na imagem de saída.

```powershell
python tools/build-kust.py
python tools/pack-image.py --boot boot.bin --loader loader.bin --kernel kernel.bin --disk disco-base.img --output gpt-r-build.img
```

Use `--zig` no bootstrap se o executável estiver fora do PATH. A imagem-base deve ter espaço no FAT16 e aceitar a instalação do bundle. O empacotador aceita repetir um bundle idêntico e indica colisões com arquivos diferentes. Testes do compilador em host e instruções para o QEMU estão na documentação. [Layout de boot e extração](docs/boot-layout.md) · [Bootstrap e testes do Kust](docs/kust-compiler.md)

## Licença

[GNU General Public License v3.0](LICENSE.md).
