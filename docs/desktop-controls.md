# Ferramentas do desktop

Abra o menu com `F1` e use as setas e `Enter` para escolher uma ferramenta.

## Gerenciador de tarefas

A coluna **CPU %** usa as amostras do timer PIT no último intervalo de
medição. Cada tick conta para o aplicativo que estava em modo usuário.
Trabalhos muito curtos podem terminar entre amostras. **KiB** soma as
páginas de imagem, pilha, heap e buffers de arquivo do processo.
**Tempo s** conta os segundos entre o início e o encerramento, incluindo
o tempo em espera. Após encerrar, a janela continua na lista com o tempo
final e a memória liberada.

Selecione um aplicativo e clique em **Trazer** ou pressione `Enter` para
focar sua janela. Um traço ao lado do nome indica uma janela minimizada.
O botão **Encerrar** e a tecla `Delete` encerram o processo.

## Relógio e Configurações

O relógio na barra lê a hora do RTC. Para mostrar a hora local do host
no QEMU, acrescente `-rtc base=localtime` ao comando da máquina virtual.

Em **Configuracoes**, clique no tema ou pressione `1` para alternar entre
os fundos azul e verde. Clique no relógio ou pressione `2` para alternar
entre 12h e 24h. Cada alteração bem-sucedida é salva em
`/CONFIG/DESKTOP.DAT`. O padrão é fundo azul e relógio de 24h.

## Lixeira

No Explorador, selecione um arquivo e pressione `Delete` ou escolha
**Enviar Lixeira** no menu de contexto. Cada arquivo é guardado com seu
caminho original em `/CONFIG/LIXEIRA`.

Abra **Lixeira** pelo menu, selecione o arquivo e pressione `Enter` ou
clique em **Restaurar**. Se o caminho original estiver indisponível, o nome
já existir ou faltar espaço para a cópia FAT16, a Lixeira guarda o arquivo
para uma nova tentativa. O comando `rm` do Terminal faz remoção direta.
