# GPT-R OS

Sistema operacional experimental x86-64 escrito em C e Assembly. Inicia via BIOS com bootloader próprio e é desenvolvido para testes no QEMU.

A **v0.6.5** adiciona um Explorador de arquivos com subpastas FAT16, um Gerenciador de tarefas e atalhos salvos no disco. A **Calculadora** é o primeiro `.APP` com interface gráfica e botões clicáveis. O desktop roda em **1024×768** via VBE, com mouse PS/2 e fonte bitmap em escala 2×. O Terminal tem 56 colunas por 27 linhas.

O desktop recompõe apenas as áreas afetadas quando um app gráfico atualiza a janela. O Explorador evita reler a pasta aberta quando as alterações no FAT16 afetam outra pasta.

## Rodar no QEMU

Coloque `gpt-r-hd.img` em uma pasta e abra o PowerShell nela. A imagem contém o bootloader, o kernel e a partição FAT16 com os arquivos de exemplo. Ajuste o caminho do QEMU se ele foi instalado em outra pasta.

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=gpt-r-hd.img,format=raw,if=ide,index=0,snapshot=on" -boot c
```

Com `snapshot=on`, as alterações no disco ficam em uma camada temporária e são descartadas ao fechar o QEMU.

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

Abra **F1 → Arquivos** para ver a raiz do disco. A lista exibe ícones, nomes, tipos e tamanhos em bytes. Use **Nova pasta** para criar uma subpasta, **Voltar** para visitar o local anterior e **Subir** para ir à pasta acima. Duplo clique ou **Enter** abre a pasta selecionada, abre `.txt` e `.asm` no Editor ou executa um `.APP`. O Editor salva no mesmo diretório em que o arquivo foi aberto.

| Pasta | Conteúdo |
|---|---|
| `HOME/DOCS/` | Espaço para documentos de texto. |
| `HOME/FONTES/` | Fontes Assembly. Inclui uma cópia de `ola.asm`. |
| `APPS/` | Programas instalados, incluindo `CALC.APP`. |
| `CONFIG/` | Dados do desktop. O sistema grava os atalhos em `ATALHOS.DAT`. |

Clique com o botão direito em um item do Explorador e escolha **Criar atalho**. Arraste o ícone no desktop para mudar sua posição. Para conferir se a posição persiste depois de fechar o QEMU, use o modo de disco gravável descrito em [Testar a persistência](#testar-a-persistência).

### Gerenciador de tarefas

Abra **F1 → Tarefas** para ver o PID, o nome, o estado e a visibilidade da janela de cada `.APP`. Selecione um processo e clique em **Encerrar APP**, inclusive se a janela estiver minimizada. O kernel libera a memória do processo e a janela mostra o resultado até você fechá-la.

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
| `version` | Mostra a versão do GPT-R OS. |
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
| `ls` | Lista os nomes, os tamanhos em bytes e as datas de modificação gravadas no diretório raiz do FAT16. |
| `cat <arquivo>` | Exibe um arquivo de texto UTF-8 com paginação. |
| `run <arquivo.APP> [argumentos]` | Executa um programa x86-64 em modo usuário e abre uma janela com o nome do executável. |
| `edit <arquivo>` | Abre o editor gráfico de texto UTF-8. |
| `asm <fonte.asm> <saida.APP>` | Monta um fonte Assembly e grava um executável no FAT16. |
| `appinfo <arquivo.APP>` | Valida e mostra o cabeçalho do executável. |
| `write <arquivo> <texto>` | Cria ou sobrescreve um arquivo de texto UTF-8. Aceita os acentos digitáveis no layout ABNT2. |
| `rm <arquivo>` | Remove um arquivo e libera seus clusters. |
| `demo` | Abre a demonstração interativa de vídeo no modo gráfico. |

Na `demo`, use as **setas** para mover o quadrado. O timer PIT anima a cor enquanto a demonstração está aberta. Pressione **Esc** para voltar ao terminal com o histórico e as cores preservados.

O heap aceita de 1 a 4.064 bytes por alocação. O shell mantém até 32 blocos ativos. Uma página volta ao alocador físico quando todos os seus blocos são liberados.

### Arquivos e paginação

Use `ls` para encontrar `README.txt` e `cat README.txt` para ler o arquivo. Na pausa do `cat`, **Espaço** avança uma página, **Enter** avança uma linha e **Esc** volta ao prompt. Ao chegar ao fim, aparece `FIM DO ARQUIVO - FAT16 OK`.

O Explorador navega por subpastas FAT16. Os comandos de arquivos do shell operam no diretório raiz. O FAT16 usa nomes curtos 8.3. Digite o nome exatamente como aparece no `ls`. Para criar arquivos, use letras sem acento, números, hífen ou sublinhado. Use uma única caixa em cada parte do nome e a extensão `.txt` em minúsculas para novos arquivos de texto. Em `nota.txt`, por exemplo, tanto o nome quanto a extensão estão em minúsculas.

Use `diskinfo` antes e depois de `write nota.txt Pão com ovo` para ver o espaço livre mudar. Leia o resultado com `cat nota.txt` e use `rm nota.txt` para liberar os clusters do arquivo. O comando `sector 0` inspeciona os primeiros bytes do MBR. Na imagem de exemplo, a partição FAT16 começa no setor 2048.

### Executar vários programas

Cada execução de `run` tem sua própria janela, com saída, pilha e contexto de CPU separados. O desktop suporta até quatro janelas de `.APP`. Quando um programa pede entrada, ele recebe as teclas digitadas na sua janela enquanto ela estiver em foco. **Ctrl+C** encerra o programa em foco. Quando um programa sai ou falha, o kernel libera sua memória e mantém a janela aberta para você ler a saída. Feche a janela para liberar espaço para outro programa.

A imagem de exemplo inclui `loop.APP` e `nome.APP` para testar a multitarefa.

1. No Terminal, execute `run loop.APP`.
2. Clique em **Terminal** na barra de tarefas e execute `run nome.APP`.
3. Na janela `nome.APP`, digite seu nome e pressione **Enter**. A saudação e o código de saída aparecem nessa janela.
4. Selecione `loop.APP` e pressione **Ctrl+C**. A mensagem de interrupção aparece na janela do loop.

### Crie um programa dentro do GPT-R OS

A imagem também vem com `ola.APP`, `ola.asm` e `README.txt`. Abra `edit ola.asm` e mude a mensagem na linha com `db`. O editor mostra números de linha, cursor, rolagem e os atalhos no rodapé. Use **Ctrl+S** para salvar e **Esc** para fechar o editor.

```text
asm ola.asm feito.APP
appinfo feito.APP
run feito.APP
```

O programa imprime a sua mensagem na janela `feito.APP` e sai com código 0. O montador suporta registradores de 32 e 64 bits, operações aritméticas, memória, saltos condicionais e rotinas com `call` e `ret`. Veja a sintaxe completa, um exemplo de leitura do teclado e os testes de exceção na [documentação do formato .APP](docs/app-format.md).

O editor aceita texto UTF-8 de até 4 KiB e salva as quebras de linha em LF. Se houver alterações pendentes ao apertar **Esc**, ele oferece salvar, descartar ou continuar.

### Testar a persistência

Use uma cópia da imagem para conservar o arquivo original. Abra a cópia no QEMU sem `snapshot=on`.

```powershell
if (!(Test-Path gpt-r-hd-teste.img)) {
    Copy-Item gpt-r-hd.img gpt-r-hd-teste.img
}
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=gpt-r-hd-teste.img,format=raw,if=ide,index=0" -boot c
```

Crie um atalho para `CALC.APP`, arraste o ícone e feche o QEMU. Reabra a mesma cópia para conferir o atalho na posição salva. Para testar texto pelo shell, salve com `write nota.txt Pão com ovo` e confira com `cat nota.txt` depois de reabrir.

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
| `kernel/desktop/` | Área de trabalho, janelas, Explorador, Gerenciador de tarefas, atalhos e desenho gráfico dos apps. |
| `kernel/memory/` | Paginação, mapa E820, alocador de páginas físicas e heap. |
| `kernel/interrupts/` | IDT, entradas de IRQ e diagnóstico de exceções. |
| `kernel/drivers/` | Teclado PS/2 ABNT2, mouse PS/2, timer PIT, console gráfico VBE e disco ATA PIO. |
| `kernel/fs/` | Leitura e escrita de arquivos e pastas FAT16. |
| `kernel/shell/` | Prompt, histórico, comandos, editor gráfico e módulos de disco, memória e diagnóstico. |
| `apps/` | Código-fonte dos programas de exemplo e da Calculadora. |
| `tools/Pack-App.ps1` | Empacotador de executáveis `.APP` para Windows. |
| `docs/app-format.md` | Formato `GTRA`, chamadas de texto e vídeo, eventos de mouse, sintaxe do montador e exemplos. |

## Licença

[GNU General Public License v3.0](LICENSE.md).