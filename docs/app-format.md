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

O shell compara o nome exatamente como aparece no `ls`. A imagem de teste contém `ola.APP` e `ola.asm`. Rode `run ola.APP` para ver a mensagem de exemplo e o código de saída 0. Os outros exemplos estão em `apps/` para quem quiser montá-los. Durante um programa ativo, **Ctrl+C** devolve o controle ao shell.

O código e as constantes ficam em uma página de leitura e execução. A pilha tem 4 KiB com uma página de guarda abaixo dela. A pilha tem execução desabilitada quando a CPU oferece NX. Ao encerrar ou interromper o programa com **Ctrl+C**, o kernel libera as duas páginas e volta ao shell.

## Montar um .APP dentro do GPT-R OS

A imagem de HD inclui o fonte `ola.asm`. No shell, rode:

```text
edit ola.asm
asm ola.asm feito.APP
appinfo feito.APP
run feito.APP
```

No editor, use **Ctrl+S** para salvar e **Esc** para voltar ao shell. Se houver alterações pendentes, o editor oferece salvar, descartar ou continuar editando. Ele abre arquivos UTF-8 de até 4 KiB e salva as quebras de linha em LF.

Com o fonte de exemplo, o programa imprime `Ola do GPT-R OS!` e sai com código 0. Para conservar o arquivo montado após fechar o QEMU, use uma cópia do HD com gravação persistente.

O comando `asm` monta um subconjunto da sintaxe Intel x86-64. O fonte e a imagem de código e dados têm limite de 4 KiB cada. Os nomes de rótulos distinguem maiúsculas de minúsculas. Use `;` para iniciar um comentário.

| Sintaxe | Operação |
|---|---|
| `rotulo:`, `entry rotulo` | Define um endereço e, opcionalmente, o ponto de entrada. Por padrão a entrada é o primeiro byte do código. |
| `mov eax, 1`, `mov rdi, rsp` | Move um imediato ou copia entre registradores de mesma largura. Aceita decimal, hexadecimal com `0x` e diferenças de rótulos como `fim - mensagem`. |
| `mov rsi, [rsp + 8]`, `mov [rsp - 8], rax` | Lê ou grava 32 ou 64 bits na memória, conforme o registrador. O endereço aceita um registrador de 64 bits com deslocamento opcional. |
| `movzx eax, byte [rdi]` | Lê um byte e preenche o restante do registrador com zeros. |
| `lea rdi, [rip + rotulo]` | Coloca o endereço de um rótulo em um registrador de 64 bits. |
| `add`, `sub`, `and`, `or`, `xor`, `cmp`, `test` | Operam entre dois registradores de mesma largura ou entre um registrador e um imediato. |
| `inc`, `dec`, `imul` | Incrementa ou decrementa um registrador. `imul` multiplica dois registradores de mesma largura e guarda o resultado no primeiro. |
| `div`, `idiv` | Divide pelo registrador indicado. `div` usa divisão sem sinal e `idiv` usa divisão com sinal. O dividendo fica em `EDX:EAX` ou `RDX:RAX` conforme a largura. |
| `jmp`, `jz`/`je`, `jnz`/`jne` | Saltos para rótulos. Os condicionais testam se o resultado anterior foi zero ou diferente de zero. |
| `jl`, `jle`, `jg`, `jge` | Saltos para comparações com sinal. |
| `jb`, `jbe`, `ja`, `jae` | Saltos para comparações sem sinal. |
| `call rotulo`, `ret` | Chama uma rotina e volta ao ponto seguinte à chamada. Para sair do programa, use a chamada de sistema `exit`. |
| `push rax`, `pop rax` | Empilha ou desempilha um registrador de 64 bits. |
| `int 0x80` | Chama o kernel. |
| `nop`, `ud2`, `int3`, `hlt` | Operação vazia e instruções para testar exceções. |
| `db "Texto", 10` | Grava strings e números de 0 a 255 como bytes. |

Os registradores aceitos são `eax`, `ecx`, `edx`, `ebx`, `esp`, `ebp`, `esi`, `edi` e as versões de 64 bits de `rax` a `rdi`. Os imediatos têm 32 bits. Em operações de 64 bits, a CPU estende o sinal do imediato.

O montador informa a linha do erro e só grava a saída depois de montar o fonte inteiro. O `appinfo` verifica a assinatura, os campos do cabeçalho e o tamanho do arquivo antes de mostrar a versão do formato e o ponto de entrada.

### Exemplo com entrada do teclado

Crie `eco.asm` com `edit eco.asm` e escreva:

```asm
    sub rsp, 128
    mov rdi, rsp
    mov esi, 128
    mov eax, 3
    int 0x80

    test eax, eax
    jle sair
    mov esi, eax
    mov rdi, rsp
    mov eax, 1
    int 0x80

sair:
    xor edi, edi
    mov eax, 2
    int 0x80
```

Salve, saia do editor e rode `asm eco.asm eco.APP` seguido de `run eco.APP`. Digite uma linha e pressione Enter. O programa imprime o texto digitado.

### Testar exceções em modo usuário

Monte um programa separado para cada teste. O shell mostra o vetor e retoma o prompt quando o programa sofre a exceção.

| Fonte do teste | Exceção esperada |
|---|---|
| `mov ecx, 0` e depois `div ecx` | Divisão por zero, vetor 0 (`#DE`). |
| `int3` | Breakpoint, vetor 3 (`#BP`). |
| `ud2` | Instrução inválida, vetor 6 (`#UD`). |
| `hlt` | Instrução privilegiada em ring 3, vetor 13 (`#GP`). |
| `xor edi, edi` e depois `mov eax, [rdi]` | Acesso a endereço inacessível para o programa, vetor 14 (`#PF`). |
