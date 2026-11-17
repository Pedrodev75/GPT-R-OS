# Pipes e redirecionamento no Terminal

```text
echo Ola | cat
ls /HOME > lista.txt
cat /HOME/DOCS/entrada.txt | cat > copia.txt
run ola.APP | cat
```

O operador `|` passa a saída de uma etapa à entrada da próxima. O operador
`>` cria ou substitui um arquivo FAT16 com a saída da última etapa. O nome
de destino segue as regras 8.3 do Terminal. Use `\|` e `\>` para inserir os
sinais como texto nos argumentos. Consulte `man pipe` dentro do sistema.

`cat` e `run` aceitam entrada de um pipe. Para ler a entrada, use `cat`
sem argumento após `|`. `cat <arquivo>` em um pipe ou com `>` copia os
bytes do arquivo, incluindo quebras de linha e bytes NUL. Na tela, o
Terminal continua exibindo texto UTF-8. As etapas encadeadas dispensam
paginação.

`echo`, `help`, `man`, `version`, `uptime`, `mem`, `diskinfo`, `sector`,
`ls`, `pwd`, `appinfo`, `cat` e `run` fornecem saída para `|` e `>`.
Comandos que editam arquivos, mudam a sessão ou abrem ferramentas pela
interface gráfica são executados diretamente no prompt.

O Terminal aceita até oito comandos. Etapas `run` ligadas diretamente ou
por `cat` sem argumentos avançam juntas, com uma fila de 4 KiB entre
aplicativos. Um leitor espera por bytes enquanto o produtor estiver aberto.
Ele recebe EOF quando o produtor termina e a fila fica vazia. Um escritor
espera espaço quando a fila enche. Se um leitor encerrar normalmente
antes de EOF, o Terminal encerra os produtores anteriores desse trecho.

As etapas que capturam toda a saída, incluindo o redirecionamento para
arquivo, mantêm o limite de 64 KiB. O `>` só grava após o sucesso da
sequência e avisa se o destino mudou durante a execução. Mensagens de
erro e códigos de saída ficam visíveis no Terminal.

Um `.APP` usado com `|` ou `>` roda em primeiro plano, sem janela própria.
Sem um pipe de entrada, `read` continua lendo uma linha do teclado. Com um
pipe de entrada, a mesma chamada recebe até `capacidade - 1` bytes e
acrescenta um NUL após o bloco. O retorno informa quantos bytes foram lidos
e vale zero no fim da entrada. Use capacidade de pelo menos dois bytes e
o tamanho retornado para processar dados binários. O desktop continua
recebendo eventos durante a execução. Com o Terminal em foco, `Ctrl+C`
interrompe o encadeamento em primeiro plano.
