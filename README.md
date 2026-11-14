# GPT-R OS

GPT-R OS é um sistema operacional experimental para x86-64, escrito em C e Assembly. Ele inicia via BIOS com um bootloader próprio. A versão **v0.2.0** traz um shell de texto e uma tela de diagnóstico para exceções da CPU.

## Rodar no QEMU

Com o QEMU instalado no Windows, coloque a imagem `GPT-R-OS_V0.2.0.img` na raiz do projeto. Abra o PowerShell nessa pasta e execute:

```powershell
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -drive "file=GPT-R-OS_V0.2.0.img,format=raw,if=floppy" -boot a
```

Se o QEMU foi instalado em outro local, ajuste o caminho do executável. A imagem `os.img` é distribuída separadamente do código-fonte.

## Shell

Digite um comando no prompt `>` e pressione **Enter**. Use **Backspace** para corrigir o texto.

| Comando | Resultado |
|---|---|
| `help` | Lista os comandos disponíveis. |
| `clear` | Limpa o terminal e preserva o cabeçalho e o contador de tempo. |
| `version` | Mostra `GPT-R OS v0.2.0`. |
| `uptime` | Mostra os segundos decorridos desde o boot. |

## Recursos

- Console VGA em modo texto com cursor piscante.
- Teclado PS/2 com layout ABNT2, acentos e atendimento pela IRQ1.
- Timer PIT pela IRQ0 com contador de tempo ligado.
- IDT de 64 bits com tela de exceção que exibe nome da falha, vetor, código de erro e registradores. Em falhas de página, exibe também o endereço em `CR2`.

## Estrutura

| Caminho | Responsabilidade |
|---|---|
| `boot/boot.S` | Carrega o kernel e coloca a CPU em modo de 64 bits. |
| `boot/layout.ld` | Define a disposição do código e dos dados na imagem. |
| `kernel/kernel.c` | Inicializa o sistema e inicia o shell. |
| `kernel/idt.c` e `kernel/exceptions.S` | Configuram a IDT e tratam exceções da CPU. |
| `kernel/keyboard.c` e `kernel/keyboard_irq.S` | Leem o teclado e atendem às interrupções do teclado e do timer. |
| `kernel/timer.c` | Configura o PIT e conta o tempo desde o boot. |
| `kernel/shell.c` | Controla o prompt e executa os comandos. |

## Licença

O GPT-R OS é licenciado sob a [GNU General Public License v3.0](LICENSE.md).