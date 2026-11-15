GPT-R OS
Sistema operacional experimental x86-64 escrito em C e Assembly. Inicia via BIOS com bootloader próprio e é desenvolvido para testes no QEMU.

A v0.4.0 roda em 640×480 via VBE, com um terminal de 49 colunas por 25 linhas. A fonte bitmap ampliada mantém os acentos, o cursor piscante e a rolagem. O driver ATA PIO permite ler setores e abrir arquivos de texto em volumes FAT16. A demonstração gráfica também foi adaptada à resolução maior.
Rodar no QEMU
Coloque os-vbe.img e disk-fat16.img na mesma pasta e abra o PowerShell nela. A primeira é a imagem de boot e a segunda é o disco de dados de exemplo. Ajuste o caminho do QEMU se ele foi instalado em outra pasta.
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 128 `
  -drive "file=os-vbe.img,format=raw,if=floppy" `
  -drive "file=disk-fat16.img,format=raw,if=ide,index=0,snapshot=on" -boot a
Para testar só o terminal, remova a opção -drive do disco FAT16.
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
diskinfo	Mostra o modelo e a capacidade acessível do disco ATA primário.
sector <LBA>	Mostra os primeiros 16 bytes de um setor. Informe o LBA em decimal.
ls	Lista os arquivos do diretório raiz do volume FAT16 e seus tamanhos.
cat <arquivo>	Exibe um arquivo de texto ASCII do diretório raiz pelo nome 8.3.
demo	Abre a demonstração interativa de vídeo no modo gráfico.


Na demo, use as setas para mover o quadrado. O timer PIT anima a cor enquanto a demonstração está aberta. Pressione Esc para voltar ao terminal com o histórico e as cores preservados.
Testar a leitura do disco
No prompt, rode diskinfo para conferir os 16.384 setores da imagem de exemplo. Digite sector 0 para inspecionar o primeiro setor e ls para localizar README.TXT. Execute cat README.TXT e confira a mensagem FIM DO ARQUIVO - FAT16 OK ao final. O arquivo ocupa três clusters não consecutivos.

O suporte a FAT16 é somente leitura e cobre arquivos no diretório raiz, com nomes 8.3. O volume pode começar no setor zero ou estar em uma partição primária MBR.
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
boot/	Bootloader BIOS, configuração VBE, coleta E820 e layout da imagem.
kernel/kernel.c	Inicializa o kernel e chama o shell.
kernel/memory/	Paginação, mapa E820 e alocador de páginas físicas.
kernel/interrupts/	IDT, entradas de IRQ e diagnóstico de exceções.
kernel/drivers/	Teclado PS/2 ABNT2, timer PIT, console gráfico VBE e disco ATA PIO.
kernel/fs/	Leitura de volumes FAT16.
kernel/shell/	Prompt e comandos do shell.


Licença
[GNU General Public License v3.0](LICENSE.md).