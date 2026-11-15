# GPT-R OS

Sistema operacional experimental x86-64 escrito em C e Assembly. Inicia via BIOS com bootloader próprio e é desenvolvido para testes no QEMU.

A **v0.4.5** adiciona escrita de arquivos FAT16 em UTF-8, um heap para o kernel e um log de inicialização antes do prompt. O shell foi dividido em módulos, com uma tabela compartilhada pelo `help` e pela execução dos comandos. O terminal gráfico roda em 640×480 via VBE, com 49 colunas por 25 linhas.

## Rodar no QEMU

Coloque `os-vbe.img` e `disk-fat16.img` na mesma pasta e abra o PowerShell nela. A primeira é a imagem de boot e a segunda é o disco de dados de exemplo. Ajuste o caminho do QEMU se ele foi instalado em outra pasta.

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=os-vbe.img,format=raw,if=floppy" `
  -drive "file=disk-fat16.img,format=raw,if=ide,index=0,snapshot=on" -boot a
```

Para testar só o terminal, remova a opção `-drive` do disco FAT16.

Com `snapshot=on`, as alterações no disco de dados ficam numa camada temporária e são descartadas ao fechar o QEMU.

## Inicialização

Antes do primeiro prompt, o sistema mostra uma linha para cada etapa do boot, incluindo vídeo, IDT, E820, alocador de páginas, heap, timer PIT, teclado PS/2, ATA PIO e montagem do FAT16.

| Indicador | Significado |
|---|---|
| `[  OK  ]` | A etapa foi concluída ou a checagem passou. |
| `[FAILED]` | A etapa encontrou uma falha, como um disco detectado sem volume FAT16 válido. |
| `[ SKIP ]` | A etapa foi pulada, como a montagem do FAT16 quando a VM está sem disco de dados. |

O shell abre mesmo sem um volume FAT16 montado.

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
| `diskinfo` | Mostra o modelo e a capacidade acessível do disco ATA primário. |
| `sector <LBA>` | Mostra os primeiros 16 bytes de um setor. Informe o LBA em decimal. |
| `ls` | Lista os arquivos do diretório raiz do volume FAT16 e seus tamanhos. |
| `cat <arquivo>` | Exibe um arquivo de texto UTF-8 do diretório raiz pelo nome 8.3. |
| `write <arquivo> <texto>` | Cria ou sobrescreve um arquivo de texto UTF-8. Aceita os acentos digitáveis no layout ABNT2. |
| `rm <arquivo>` | Remove um arquivo e libera seus clusters. |
| `demo` | Abre a demonstração interativa de vídeo no modo gráfico. |

Na `demo`, use as **setas** para mover o quadrado. O timer PIT anima a cor enquanto a demonstração está aberta. Pressione **Esc** para voltar ao terminal com o histórico e as cores preservados.

O heap aceita de 1 a 4.064 bytes por alocação. O shell mantém até 32 blocos ativos. Uma página volta ao alocador físico quando todos os seus blocos são liberados.

### Testar o disco

No prompt, rode `diskinfo` para conferir os 16.384 setores da imagem de exemplo. Digite `sector 0` para inspecionar o primeiro setor e `ls` para localizar `README.TXT`. Execute `cat README.TXT` e confira a mensagem `FIM DO ARQUIVO - FAT16 OK` ao final. O arquivo ocupa três clusters não consecutivos.

O FAT16 suporta arquivos no diretório raiz com nomes 8.3. Para criar ou editar nomes pelo shell, use letras sem acento, números, hífen e sublinhado. O volume pode começar no setor zero ou estar em uma partição primária MBR.

### Testar a persistência

Use uma cópia do disco de exemplo para conservar a imagem original. Na mesma pasta, abra a cópia no QEMU sem `snapshot=on`:

```powershell
if (!(Test-Path disk-fat16-teste.img)) {
    Copy-Item disk-fat16.img disk-fat16-teste.img
}
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=os-vbe.img,format=raw,if=floppy" `
  -drive "file=disk-fat16-teste.img,format=raw,if=ide,index=0" -boot a
```

Digite `write NOTA.TXT Pão com ovo` e `cat NOTA.TXT`. Feche o QEMU, abra novamente com o mesmo comando e rode `cat NOTA.TXT`. O arquivo deve continuar exibindo `Pão com ovo`. Para testar a remoção, rode `rm NOTA.TXT` e confira o resultado com `ls`.

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

Os testes de exceção param na tela de diagnóstico. Ela mostra o vetor, o código de erro e os registradores. Para comparar `mem`, inicie o QEMU uma vez com `-m 64` e outra com `-m 128`.

## Estrutura

| Caminho | Responsabilidade |
|---|---|
| `boot/` | Bootloader BIOS, configuração VBE, coleta E820 e layout da imagem. |
| `kernel/kernel.c` | Inicializa o kernel, mostra o log de boot e chama o shell. |
| `kernel/memory/` | Paginação, mapa E820, alocador de páginas físicas e heap. |
| `kernel/interrupts/` | IDT, entradas de IRQ e diagnóstico de exceções. |
| `kernel/drivers/` | Teclado PS/2 ABNT2, timer PIT, console gráfico VBE e disco ATA PIO. |
| `kernel/fs/` | Leitura e escrita de volumes FAT16. |
| `kernel/shell/` | Prompt, tabela de comandos e módulos de saída, disco, memória e diagnóstico. |

## Licença

[GNU General Public License v3.0](LICENSE.md).
