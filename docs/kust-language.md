# Kust 0.1

Kust compila fontes `.KST` para aplicativos nativos GTRA v1.1. Nas
imagens Kust Edition, o Terminal resolve `kust` em `/SYS64/KUST.APP`.
O manual offline fica em `/SYS64/DOCS/KUST` e pode ser aberto no
Explorador ou com `kust docs [CAPITULO [PAG]]`. Use
`kust docs --all CAPITULO | cat` para ler um capítulo inteiro pelo Terminal.

```kust
app "Contador"

const LIMITE: i32 = 3

func dobro(n: i32) -> i32 {
    return n * 2
}

func main() {
    mut atual: i32 = 0
    while atual < LIMITE {
        if atual == 1 {
            term.println("Metade!")
        } else {
            term.println(dobro(atual))
        }
        atual += 1
    }
}
```

Salve como `CONTA.KST` e execute `kust check CONTA.KST`,
`kust build CONTA.KST` e `run CONTA.APP`. Use `edit CONTA.KST` e
Ctrl+S para criar e salvar uma fonte no Editor. O diretório de trabalho
do Terminal vale para a leitura e para a saída padrão.

## Sintaxe e valores

- A primeira declaração é `app "Título"`. O título é gravado nos dados
  do binário. `func main()` é obrigatória, sem parâmetros ou tipo de retorno.
- `let nome: tipo = valor` e os parâmetros são imutáveis. `mut` permite
  atribuição com `=` e, para `i32`, com `+=` e `-=`. `const` pode ser
  local ou global e exige uma expressão constante. Expressões constantes
  podem usar outras constantes já declaradas.
- Um nome local vale da declaração até o fim do bloco. Um bloco interno
  pode sombrear um nome externo. Funções têm assinaturas coletadas antes
  de seus corpos e podem chamar funções declaradas mais adiante.
- `i32` aceita literais decimais no intervalo de -2.147.483.648 a
  2.147.483.647. `bool` aceita `true` e `false`. Ambos são tipos distintos.
  Strings são literais UTF-8 aceitos por APIs específicas. Os escapes são
  `\n`, `\r`, `\t`, `\"` e `\\`.
- Identificadores são ASCII e distinguem maiúsculas e minúsculas. Aceitam
  letras, dígitos e sublinhado com a restrição de que o primeiro caractere
  seja letra ou sublinhado. Comentários usam `//` ou `/* ... */` sem
  aninhamento. O ponto e vírgula é opcional.

| Precedência, da maior para a menor | Operadores |
| --- | --- |
| Unários | `-`, `!` |
| Multiplicativos | `*`, `/`, `%` |
| Aditivos | `+`, `-` |
| Comparação de inteiros | `<`, `<=`, `>`, `>=` |
| Igualdade entre valores do mesmo tipo | `==`, `!=` |
| E lógico | `&&` |
| Ou lógico | `\|\|` |

Operadores binários de mesma precedência associam da esquerda para a
direita. Operadores lógicos avaliam o segundo operando só quando
necessário. Aritmética `i32` conserva os 32 bits inferiores. A divisão
inteira trunca em direção a zero e o resto tem o sinal do dividendo.
`MIN / -1` produz `MIN` e `MIN % -1` produz zero. Em uma expressão
constante avaliada, dividir por zero é erro E008. Em execução, o
programa informa R001 e sai com código 1.

`if`, `else` e `while` usam blocos delimitados por chaves. As condições
precisam ser `bool`. Funções com `-> i32` ou `-> bool` precisam
retornar em todos os caminhos alcançáveis, salvo encerramento explícito
do aplicativo. A análise considera que um laço pode executar zero vezes.
Use `return` em uma linha própria ou `return;` para sair de uma função
sem valor. Os argumentos são avaliados da esquerda para a direita.

## APIs

| API | Tipos e resultados |
| --- | --- |
| `term.print(valor)`, `term.println(valor)` | Recebem um literal string, `i32` ou `bool`. `println` acrescenta LF. |
| `app.exit(codigo)` | Encerra o aplicativo com um código `i32`. |
| `file.openRead("caminho")`, `file.openWrite("caminho")` | Retornam um handle `i32` ou erro negativo. Use nomes FAT16 8.3 e extensão `.txt` ao criar arquivos de texto. |
| `file.readByte(handle)` | Retorna 0 a 255, EOF -11 ou erro negativo da ABI. |
| `file.write(handle, "texto")` | Grava o literal no buffer do arquivo e retorna o número de bytes ou erro. |
| `file.close(handle)` | Fecha o handle e publica a escrita. Retorna zero ou erro. |
| `mem.alloc(tamanho)`, `mem.free()` | Alocam ou liberam um buffer por aplicativo. Tamanho de 1 a 4.096 bytes. Retornam zero ou erro. |
| `mem.read(indice)`, `mem.write(indice, byte)` | Índices baseados em zero. `read` devolve o byte ou erro. `write` recebe 0 a 255 e devolve zero ou erro. |
| `gfx.begin(rgb)`, `gfx.rect(x, y, w, h, rgb)`, `gfx.present()` | Operações da janela GTRA. Inteiros em pixels e RGB de 24 bits. Retornam zero ou -1. |
| `gfx.wait()` | Aguarda um clique e retorna `y * 65536 + x`, ou -1 se a janela gráfica estiver indisponível. |

Os caminhos de arquivo são resolvidos a partir do diretório herdado ou
da raiz quando começam com `/`. A ABI permite quatro handles de arquivo
por processo. Uma saída normal também fecha escritas pendentes e pode
publicá-las. Confira o retorno de `file.close` para detectar erros de
gravação. Use programas gráficos pelo Desktop, sem pipes nem
redirecionamento. O buffer de memória é zerado na alocação e seus
endereços ficam internos ao aplicativo gerado.

## Manual offline

O [índice](kust/README.TXT) aponta para `START`, `SYNTAX`, `TYPES`,
`FLOW`, `FUNCS`, `API`, `ERRORS`, `LIMITS` e `CLI`. Os exemplos
instalados espelham a [pasta de exemplos do repositório](kust/EXAMPLES).
Para editar um exemplo, copie-o do SYS64 para um diretório gravável.
