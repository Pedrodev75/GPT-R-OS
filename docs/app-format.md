# Executáveis .APP do GPT-R OS

A versão 1 do formato guarda uma imagem plana de código e dados constantes x86-64. O programa deve usar endereçamento relativo à posição e manter dados mutáveis na pilha. A execução em ring 3 e o comando `run` entram no próximo bloco da v0.5.0.

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

A ABI prevista usa `int 0x80`. O código inicia com uma pilha alinhada a 16 bytes, sem argumentos. Ao terminar, deve chamar `exit`.

| RAX | Chamada | Argumentos e retorno |
|---|---|---|
| 1 | `write` | RDI aponta para o texto UTF-8, RSI informa o comprimento em bytes. RAX retorna o número de bytes consumidos ou um erro negativo. O kernel verifica toda a faixa indicada. |
| 2 | `exit` | RDI contém o código de saída. O kernel encerra o programa e volta ao shell. |

## Gerar OLA.APP no Windows

Na raiz do projeto, abra o PowerShell e rode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\Pack-App.ps1 -Message "Ola do meu programa" -OutputPath OLA.APP
```

O script já contém o código equivalente ao exemplo em `apps/OLA.S`. Sem `-Message`, gera a mensagem `Ola, GPT-R OS!`. Para empacotar outro código x86-64 que você já montou como binário plano, passe `-InputPath programa.bin` e, se necessário, `-EntryOffset`.

A imagem de HD de teste já contém `OLA.APP` e `README.TXT` no FAT16. Use `ls` para conferir a presença do programa.
