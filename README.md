# GPT-R OS

GPT-R OS é um sistema operacional experimental para x86-64, escrito em C e Assembly. Ele inicia via BIOS com um bootloader próprio e roda no QEMU.

Na **v0.2.5**, o bootloader coleta o mapa de memória da BIOS por E820. O shell ganhou o comando `mem`, e o kernel passou a reservar e liberar páginas físicas de 4 KiB. Os arquivos do kernel foram organizados por função.

## Rodar no QEMU

Com o QEMU instalado no Windows, coloque a imagem da v0.2.5 na raiz do projeto com o nome `os.img`. Abra o PowerShell nessa pasta e rode:

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 -drive "file=os.img,format=raw,if=floppy" -boot a
```

Se o QEMU estiver instalado em outra pasta, ajuste o caminho do executável.

## Shell

Digite um comando no prompt `>` e pressione **Enter**. Use **Backspace** para corrigir a linha.

| Comando | Resultado |
|---|---|
| `help` | Lista os comandos disponíveis. |
| `clear` | Limpa o terminal e preserva o cabeçalho e o contador de tempo. |
| `version` | Mostra `GPT-R OS v0.2.5`. |
| `uptime` | Mostra os segundos decorridos desde o boot. |
| `mem` | Lista as regiões E820, a RAM utilizável reportada pela BIOS e as páginas de 4 KiB livres. |

O alocador gerencia páginas entre **1 MiB e 2 MiB**, dentro da faixa mapeada pelo kernel. O total mostrado por `mem` inclui a RAM utilizável que a BIOS reporta fora dessa faixa.

## Recursos

- Console VGA em modo texto com cursor piscante.
- Teclado PS/2 com layout ABNT2 e atendimento pela IRQ1.
- Timer PIT pela IRQ0 com contador de tempo ligado.
- IDT de 64 bits com tela de exceção que mostra o nome da falha, o vetor, o código de erro e os registradores. Em falhas de página, também exibe o endereço em `CR2`.

## Estrutura

| Caminho | Responsabilidade |
|---|---|
| `boot/` | Bootloader BIOS, coleta do mapa E820 e layout da imagem. |
| `kernel/kernel.c` | Inicializa o kernel e chama o shell. |
| `kernel/memory/` | Estrutura do mapa E820 e alocador de páginas físicas. |
| `kernel/interrupts/` | IDT e rotinas de entrada das exceções e IRQs. |
| `kernel/drivers/` | Drivers do teclado PS/2 e do timer PIT. |
| `kernel/shell/` | Prompt e comandos do shell. |

## Licença

O GPT-R OS é licenciado sob a [GNU General Public License v3.0](LICENSE.md).