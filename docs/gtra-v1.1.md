# GTRA v1.1

GTRA v1.1 acrescenta imagens multipágina, dados graváveis e BSS. O formato mantém a assinatura `GTRA` e usa a versão binária `0x0101`. A versão binária `0x0001` continua usando o [formato e o layout v1](app-format.md).

## Cabeçalho e arquivo

Os inteiros são little-endian. O arquivo tem exatamente `64 + code_size + data_size` bytes, sem preenchimento entre seções. O BSS não ocupa espaço em disco.

| Offset | Tipo | Campo |
| --- | --- | --- |
| `0x00` | 4 bytes | Assinatura ASCII `GTRA` |
| `0x04` | u16 | Versão `0x0101` |
| `0x06` | u16 | Tamanho do cabeçalho, 64 |
| `0x08` | u32 | Offset de código, 64 |
| `0x0C` | u32 | Tamanho do código e das constantes |
| `0x10` | u32 | Offset dos dados, `64 + code_size` |
| `0x14` | u32 | Tamanho dos dados inicializados |
| `0x18` | u32 | Tamanho do BSS |
| `0x1C` | u32 | Entrada relativa ao início do código |
| `0x20` | 32 bytes | Reservados, todos zero |

O código precisa conter pelo menos um byte. A entrada deve cair dentro do código. Código, dados e BSS somados podem ocupar até 65.536 bytes. Os dados e o BSS podem ter tamanho zero. O carregador valida offsets, tamanho e cadeia FAT16. Depois de validar o cabeçalho, reserva páginas e copia o conteúdo. As sobras das páginas e o BSS começam zerados.

## Mapa virtual

O início do intervalo é inclusivo e o fim é exclusivo. A execução v1.1 requer NX. Cada processo recebe as próprias páginas físicas.

| Intervalo | Região |
| --- | --- |
| `0x80000000–0x80010000` | Código e constantes RX |
| `0x80010000–0x80011000` | Guarda |
| `0x80011000–0x80021000` | Dados e BSS RW com NX |
| `0x80021000–0x80022000` | Guarda inferior da pilha |
| `0x80022000–0x80026000` | Pilha de 16 KiB RW com NX |
| `0x80026000–0x80027000` | Guarda superior da pilha |
| `0x80027000–0x80067000` | Heap de até 256 KiB RW com NX, sob demanda |
| `0x80067000–0x80200000` | Sem mapeamento |

A pilha cresce para baixo. Na entrada, `RSP` está alinhado a 16 bytes e aponta para `argc`, seguido de `argv[]` e do ponteiro nulo final. Os limites continuam em 40 argumentos e 256 bytes com terminadores. O processo herda o diretório de trabalho do chamador. IDs e convenções das syscalls GTRA v1 se aplicam à v1.1.

## Montagem no GPT-R OS

`asm fonte.asm saida.APP` continua emitindo v1. Use `asm -f v1.1 fonte.asm saida.APP` para v1.1. A versão nova aceita até 64 KiB de fonte e 1.024 rótulos. Por padrão o fonte começa em `.text`. `db` emite bytes em `.text` e `.data`. Use `resb` em `.bss`. Instruções, entrada e alvos de saltos diretos ficam em `.text`. `lea` com RIP relativo pode referenciar todas as seções.

```asm
.section .data
mensagem: db "Ola, GTRA v1.1", 10
fim:

.section .bss
contador: resb 8

.section .text
entry inicio
inicio:
    lea rbx, [rip + contador]
    mov eax, [rbx]
    inc eax
    mov [rbx], eax
    mov eax, 1
    lea rdi, [rip + mensagem]
    mov esi, fim - mensagem
    int 0x80
    mov eax, 2
    xor edi, edi
    int 0x80
```

`appinfo saida.APP` mostra a versão, os tamanhos das seções, o total da imagem e a entrada. A montagem e a carga ainda são síncronas. Arquivos grandes podem atrasar eventos enquanto o kernel lê ou grava no FAT16.

A Central aceita executáveis principais v1 ou v1.1 em GTRP v1. O teto do pacote continua em 256 KiB. Para gerar um executável fora do GPT-R OS, `Pack-App.ps1` recebe `-Format v1.1`, `-InputPath` para código, `-DataPath` opcional, `-BssSize` e `-EntryOffset`. Sem a opção de formato, gera v1. O teto de 64 KiB por handle de gravação GTRA continua igual. Um executável v1.1 com o máximo de código e dados em disco ocupa 65.600 bytes.

## Argumentos do Terminal

Em `run programa.APP "dois termos" "" "a|b>c"`, chegam três argumentos depois de `argv[0]`. O segundo está vazio. Pipe e redirecionamento só operam fora das aspas de um argumento. Dentro das aspas, `\"` representa uma aspa e `\\` uma barra invertida. Fora delas, `\|` e `\>` preservam os caracteres como texto. Aspas abertas no fim da linha geram erro antes da execução.

## Teste automatizado no host

Faça o teste em um host x86-64 com GCC ou Clang. Ele usa o carregador e o assembler do kernel com um alocador e um leitor FAT16 de teste. As rotinas privilegiadas ficam fora da execução.

```sh
cc -std=c11 -Wall -Wextra -Werror tests/gtra_v11.c kernel/apps/assembler.c kernel/apps/app_format.c -o gtra_v11_test
./gtra_v11_test
```
