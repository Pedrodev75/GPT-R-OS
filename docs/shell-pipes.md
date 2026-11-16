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

O Terminal executa até oito comandos em sequência e mantém até 64 KiB de
saída por etapa capturada. Um erro de leitura, falta de memória, excesso de
saída, interrupção ou falha de um programa encerra a sequência. O
redirecionamento só solicita a gravação ao FAT16 depois que todas as etapas
tiverem sucesso. Mensagens de erro do Terminal e códigos de saída ficam
visíveis no Terminal.

Um `.APP` usado com `|` ou `>` roda em primeiro plano, sem janela própria.
Sem um pipe de entrada, `read` continua lendo uma linha do teclado. Com um
pipe de entrada, a mesma chamada recebe até `capacidade - 1` bytes e
acrescenta um NUL após o bloco. O retorno informa quantos bytes foram lidos
e vale zero no fim da entrada. Use capacidade de pelo menos dois bytes e
o tamanho retornado para processar dados binários. `Ctrl+C` interrompe o
aplicativo em primeiro plano.
