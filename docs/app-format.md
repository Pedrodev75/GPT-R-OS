# Executáveis .APP do GPT-R OS

A versão 1 do formato guarda uma imagem plana de código e dados constantes x86-64. O programa deve usar endereçamento relativo à posição e manter dados mutáveis na pilha. O comando `run <arquivo.APP> [argumentos]` carrega e executa essa imagem em ring 3. O shell executa um programa por vez.

## Cabeçalho

Todos os inteiros usam little-endian. O arquivo tem exatamente 32 bytes de cabeçalho seguidos pela imagem.

| Offset | Tamanho | Campo |
|---|---|---|
| 0 | 4 bytes | Assinatura ASCII `GTRA`. |
| 4 | 2 bytes | Versão do formato, igual a 1. |
| 6 | 2 bytes | Tamanho do cabeçalho, igual a 32. |
| 8 | 4 bytes | Tamanho da imagem, de 1 a 4.096 bytes. |
| 12 | 4 bytes | Offset do ponto de entrada a partir do primeiro byte da imagem. Deve ser menor que o tamanho da imagem. |
| 16 | 16 bytes | Reservados. Devem ser zero. |

## Chamadas ao kernel

A ABI usa `int 0x80`. Ao terminar, o programa chama `exit`.

Na entrada, `RSP` está alinhado a 16 bytes e aponta para `argc` (64 bits). O vetor `argv` começa em `RSP + 8` e termina com um ponteiro nulo. `argv[0]` contém o nome do arquivo e os argumentos seguintes chegam em UTF-8, terminados em NUL. O shell separa palavras por espaços. Por exemplo, em `run eco.APP Ola mundo`, `argc` vale 3.

| RAX | Chamada | Argumentos e retorno |
|---|---|---|
| 1 | `write` | RDI aponta para o texto UTF-8, RSI informa o comprimento em bytes. RAX retorna o número de bytes consumidos ou um erro negativo. O kernel verifica toda a faixa indicada. |
| 2 | `exit` | RDI contém o código de saída. O kernel encerra o programa e volta ao shell. |
| 3 | `read` | RDI aponta para um buffer gravável na pilha e RSI indica a capacidade incluindo o NUL. Lê uma linha UTF-8 até Enter e retorna os bytes escritos, sem contar o NUL. |

Na chamada `read`, Backspace apaga o último caractere inteiro. A digitação ocupa uma linha e caracteres adicionais são ignorados quando o buffer ou a linha fica cheia. Capacidade zero retorna zero imediatamente. Um buffer fora da pilha retorna `-1`.

## Gerar OLA.APP no Windows

Na raiz do projeto, abra o PowerShell e rode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Pack-App.ps1 -Message "Ola do meu programa" -OutputPath OLA.APP
```

O script já contém o código equivalente ao exemplo em `apps/OLA.S`. Sem `-Message`, gera a mensagem `Ola, GPT-R OS!`. Para empacotar outro código x86-64 que você já montou como binário plano, passe `-InputPath programa.bin` e, se necessário, `-EntryOffset`.

O shell compara o nome exatamente como aparece no `ls`. Na imagem de HD de teste, use `ola.APP` com o nome em minúsculas e a extensão em maiúsculas.

| Comando | Resultado |
|---|---|
| `run eco.APP Ola mundo` | Mostra os argumentos separados por espaços. |
| `run nome.APP` | Pede seu nome e exibe uma saudação depois de Enter. Pressione **Ctrl+C** durante a leitura para voltar ao shell. |
| `run loop.APP` | Entra em loop. Pressione **Ctrl+C** para encerrar o programa e voltar ao shell. |
| `run ola.APP` | Exibe uma mensagem, mostra o código de saída 0 e volta ao prompt. |
| `run FALHA.APP` | Executa `ud2`, mostra o vetor 6 e volta ao prompt. |
| `run INVALIDO.APP` | Recusa um cabeçalho inválido e volta ao prompt. |

O código e as constantes ficam em uma página de leitura e execução. A pilha tem 4 KiB com uma página de guarda abaixo dela. A pilha tem execução desabilitada quando a CPU oferece NX. Ao encerrar ou interromper o programa com **Ctrl+C**, o kernel libera as duas páginas e volta ao shell.
