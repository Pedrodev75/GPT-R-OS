GPT-R OS
Sistema operacional experimental x86-64 escrito em C e Assembly. Inicia via BIOS com bootloader próprio e é desenvolvido para testes no QEMU.

A v0.3.5 traz um terminal gráfico VGA em 320×200 com 256 cores. O terminal tem 40 colunas e 18 linhas, fonte bitmap com acentos, cursor piscante e rolagem. O comando demo mostra a paleta VGA e um quadrado interativo.
Rodar no QEMU
Coloque a imagem gráfica da v0.3.5 com o nome os-vga.img na pasta atual do PowerShell. Ajuste o caminho do QEMU se ele foi instalado em outra pasta.
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 -drive "file=os-vga.img,format=raw,if=floppy" -boot a
Shell
Digite no prompt > e pressione Enter.
Comando	Resultado
help	Lista os comandos.
clear	Limpa o terminal e preserva o cabeçalho.
version	Mostra a versão do GPT-R OS.
uptime	Mostra os segundos desde o boot.
mem	Mostra o mapa E820, a RAM utilizável, a RAM mapeada para o alocador e as páginas livres.
memtest	Aloca uma página de 4 KiB, verifica os padrões 0xAA e 0x55 e libera a página.
peek <endereço>	Mostra 16 bytes a partir de um endereço hexadecimal na RAM gerenciada. Exemplo peek 0x200000.
reboot	Reinicia o sistema no QEMU PC.
color <frente> <fundo>	Muda as cores do terminal com índices VGA. Aceita frente de 0 a 15 e fundo de 0 a 7. Exemplo color 15 1.
crash <tipo>	Aciona um teste de diagnóstico.
demo	Abre a demonstração interativa de vídeo no modo gráfico.


Na demo, use as setas para mover o quadrado. O timer PIT anima a cor enquanto a demonstração está aberta. Pressione Esc para voltar ao terminal com o histórico e as cores preservados.
Diagnóstico
Tipo	Resultado esperado
ud	ud2 gera #UD (vetor 6).
div	Divisão inteira por zero gera #DE (vetor 0).
pf	Ler 0x40000000 gera #PF (vetor 14) e mostra o endereço em CR2.
gp	Ler 0x8000000000000000 gera #GP (vetor 13).
int3	int3 gera #BP (vetor 3).
alloc	Aloca até receber NULL, libera as páginas do teste e devolve o controle ao shell.
hang	Desativa as interrupções mascaráveis e entra em loop. Reinicie a VM pelo QEMU para voltar ao prompt.
triple	Carrega uma IDT vazia e provoca triple fault. Com as opções padrão, o QEMU reinicia a VM.


Os testes de exceção param na tela de diagnóstico. Ela mostra o vetor, o código de erro e os registradores. Para comparar mem, inicie o QEMU uma vez com -m 64 e outra com -m 128.
Estrutura
Caminho	Responsabilidade
boot/	Bootloader BIOS, coleta E820 e layout da imagem.
kernel/kernel.c	Inicializa o kernel e chama o shell.
kernel/memory/	Paginação, mapa E820 e alocador de páginas físicas.
kernel/interrupts/	IDT, entradas de IRQ e diagnóstico de exceções.
kernel/drivers/	Teclado PS/2 ABNT2, timer PIT e renderização gráfica VGA.
kernel/shell/	Prompt e comandos do shell.


Licença
[GNU General Public License v3.0](LICENSE.md).