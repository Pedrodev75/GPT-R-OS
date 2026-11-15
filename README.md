# GPT-R OS

Sistema operacional experimental x86-64 escrito em C e Assembly. Inicia via BIOS com bootloader próprio e é desenvolvido para testes no QEMU.

A **v0.5.0** inicia direto pelo HD virtual, executa programas `.APP` em modo usuário e amplia os comandos de armazenamento FAT16. O terminal gráfico roda em 640×480 via VBE, com 49 colunas por 25 linhas.

## Rodar no QEMU

Coloque `gpt-r-hd.img` em uma pasta e abra o PowerShell nela. A imagem contém o bootloader, o kernel e a partição FAT16 com os arquivos de exemplo. Ajuste o caminho do QEMU se ele foi instalado em outra pasta.

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=gpt-r-hd.img,format=raw,if=ide,index=0,snapshot=on" -boot c
```

Com `snapshot=on`, as alterações no disco ficam em uma camada temporária e são descartadas ao fechar o QEMU.

## Inicialização

Antes do primeiro prompt, o sistema mostra uma linha para cada etapa do boot, incluindo vídeo, IDT, E820, alocador de páginas, heap, timer PIT, teclado PS/2, ATA PIO e montagem do FAT16.

| Indicador | Significado |
|---|---|
| `[  OK  ]` | A etapa foi concluída ou a checagem passou. |
| `[FAILED]` | A etapa encontrou uma falha, como um disco detectado sem volume FAT16 válido. |
| `[ SKIP ]` | A etapa foi pulada, como a montagem do FAT16 quando a VM está sem disco de dados. |

Se o kernel iniciar mas não conseguir montar o FAT16, o shell ainda abre.

## Shell

Digite no prompt `>` e pressione **Enter**.

| Comando | Resultado |
|---|---|
| `help` | Lista os comandos. |
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
| `run <arquivo.APP>` | Executa um programa x86-64 em modo usuário. |
| `write <arquivo> <texto>` | Cria ou sobrescreve um arquivo de texto UTF-8. Aceita os acentos digitáveis no layout ABNT2. |
| `rm <arquivo>` | Remove um arquivo e libera seus clusters. |
| `demo` | Abre a demonstração interativa de vídeo no modo gráfico. |

Na `demo`, use as **setas** para mover o quadrado. O timer PIT anima a cor enquanto a demonstração está aberta. Pressione **Esc** para voltar ao terminal com o histórico e as cores preservados.

O heap aceita de 1 a 4.064 bytes por alocação. O shell mantém até 32 blocos ativos. Uma página volta ao alocador físico quando todos os seus blocos são liberados.

### Arquivos e paginação

Use `ls` para encontrar `README.txt` e `cat README.txt` para ler o arquivo. Na pausa do `cat`, **Espaço** avança uma página, **Enter** avança uma linha e **Esc** volta ao prompt. Ao chegar ao fim, aparece `FIM DO ARQUIVO - FAT16 OK`.

O FAT16 trabalha com o diretório raiz e nomes curtos 8.3. Digite o nome exatamente como aparece no `ls`. Para criar arquivos, use letras sem acento, números, hífen ou sublinhado. Use uma única caixa em cada parte do nome e a extensão `.txt` em minúsculas para novos arquivos de texto. Em `nota.txt`, por exemplo, tanto o nome quanto a extensão estão em minúsculas.

Use `diskinfo` antes e depois de `write nota.txt Pão com ovo` para ver o espaço livre mudar. Leia o resultado com `cat nota.txt` e use `rm nota.txt` para liberar os clusters do arquivo. O comando `sector 0` inspeciona os primeiros bytes do MBR. Na imagem de exemplo, a partição FAT16 começa no setor 2048.

### Programas em modo usuário

| Comando | Resultado |
|---|---|
| `run ola.APP` | Exibe `Ola, GPT-R OS!`, mostra o código de saída 0 e retorna ao prompt. |
| `run FALHA.APP` | Executa uma instrução inválida. O shell mostra o vetor 6 e volta ao prompt. |
| `run INVALIDO.APP` | Recusa o cabeçalho inválido e volta ao prompt. |

O shell executa um programa por vez, em ring 3. O formato binário e o empacotador para Windows estão descritos em [Formato dos programas .APP](docs/app-format.md).

### Testar a persistência

Use uma cópia da imagem para conservar o arquivo original. Abra a cópia no QEMU sem `snapshot=on`.

```powershell
if (!(Test-Path gpt-r-hd-teste.img)) {
    Copy-Item gpt-r-hd.img gpt-r-hd-teste.img
}
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=gpt-r-hd-teste.img,format=raw,if=ide,index=0" -boot c
```

Digite `write nota.txt Pão com ovo`, feche o QEMU e abra a mesma cópia de novo. Execute `cat nota.txt` para conferir o texto salvo.

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

As exceções do kernel param na tela de diagnóstico. Ela mostra o vetor, o código de erro e os registradores. Para comparar `mem`, inicie o QEMU uma vez com `-m 64` e outra com `-m 128`.

## Estrutura

| Caminho | Responsabilidade |
|---|---|
| `boot/` | Bootloader BIOS, leitura do HD, configuração VBE, coleta E820 e layout da imagem. |
| `kernel/kernel.c` | Inicializa o kernel, mostra o log de boot e chama o shell. |
| `kernel/apps/` | Carregador `.APP`, execução em modo usuário e chamadas ao kernel. |
| `kernel/memory/` | Paginação, mapa E820, alocador de páginas físicas e heap. |
| `kernel/interrupts/` | IDT, entradas de IRQ e diagnóstico de exceções. |
| `kernel/drivers/` | Teclado PS/2 ABNT2, timer PIT, console gráfico VBE e disco ATA PIO. |
| `kernel/fs/` | Leitura e escrita de volumes FAT16. |
| `kernel/shell/` | Prompt, tabela de comandos e módulos de saída, disco, memória e diagnóstico. |
| `apps/` | Código-fonte dos programas de exemplo. |
| `tools/Pack-App.ps1` | Empacotador de executáveis `.APP` para Windows. |
| `docs/app-format.md` | Formato do cabeçalho `GTRA` e chamadas ao kernel. |

## Licença

[GNU General Public License v3.0](LICENSE.md).
