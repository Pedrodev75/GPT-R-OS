# Serviço de montagem Assembly

A syscall monta um arquivo `.ASM` no assembler nativo e grava um `.APP`. Funciona com processos GTRA v1 e v1.1, em janelas e em pipes. O comando `asm` usa o mesmo serviço internamente.

## ABI

Faça a chamada com `int 0x80`.

| Registrador | Entrada | Saída |
| --- | --- | --- |
| `RAX` | `15` | `0` se o executável foi gravado ou um código de erro negativo |
| `RDI` | Ponteiro para caminho do fonte `.ASM` | Preservado |
| `RSI` | Ponteiro para caminho de destino `.APP` | Preservado |
| `RDX` | `1` para GTRA v1 ou `0x0101` para GTRA v1.1 | Linha da montagem, começando em 1. Zero em sucesso ou quando não há linha associada. |

Os caminhos terminam em NUL e têm até **221 bytes antes do NUL**. O kernel copia e valida os dois caminhos antes de iniciar a montagem. Eles podem atravessar fronteiras de páginas legíveis e obedecem às regras de caminhos e nomes 8.3 do FAT16. Caminhos relativos usam o diretório de trabalho herdado pelo processo. A chamada altera somente `RAX` e `RDX`. Os IDs e registradores de retorno das syscalls anteriores permanecem iguais.

| Retorno | Significado |
| --- | --- |
| `-1` | Ponteiro, caminho de destino, extensão ou versão inválida |
| `-2` | Fonte ou pasta do fonte ausente |
| `-3` | Destino protegido, com conflito de nome ou aberto por um aplicativo. Também indica fonte com handle de escrita ativo. |
| `-4` | Falha de leitura do fonte ou da sua cadeia FAT16 |
| `-5` | Memória insuficiente para fonte, rótulos ou imagem |
| `-6` | Limite de fonte, imagem ou rótulos excedido, disco ou diretório cheio |
| `-8` | Erro de sintaxe ou resolução de símbolo. Consulte `RDX`. |
| `-9` | Falha de preparação ou gravação do destino. A publicação não foi concluída. |
| `-10` | Falha de gravação seguida de falha ao restaurar a entrada anterior. O estado do destino é incerto. |

A versão v1 aceita 4 KiB de fonte e imagem e até 64 rótulos. A v1.1 aceita 64 KiB de fonte e de imagem em memória e até 1.024 rótulos. O serviço usa a escrita FAT16 em fluxo, então um executável v1.1 de **65.600 bytes** com cabeçalho pode ser gravado sem alterar o teto de 64 KiB da syscall de escrita comum.

## Publicação e cancelamento

O serviço resolve os caminhos e verifica o destino. Cada montagem usa suas próprias páginas temporárias. O assembler termina e o cabeçalho é validado em memória antes da escrita. O FAT16 grava uma cadeia nova, atualiza os espelhos e publica o arquivo pela entrada de diretório. Se a gravação dessa entrada reportar falha, o kernel tenta restaurar o setor anterior antes de liberar a cadeia nova. Um destino protegido ou aberto por um aplicativo gera erro antes da montagem.

Se a restauração também falhar, a chamada devolve `-10` e mantém as duas cadeias reservadas para evitar liberar clusters que ainda possam estar referenciados. Se a publicação for concluída e só a liberação da cadeia antiga falhar, o novo executável permanece válido. O Terminal exibe um aviso nessa situação. O FAT16 não tem journal. Falhas persistentes de mídia ou perda de energia exigem verificar o volume antes de confiar novamente no destino.

A chamada é **síncrona**. Ela usa o caminho de I/O do escalonador após desmontar o mapeamento do aplicativo. O kernel trata uma montagem por vez. O teclado pode enfileirar `Ctrl+C` durante a operação, mas o cancelamento do processo é atendido quando o kernel volta ao ciclo de eventos. A publicação que já terminou permanece válida. As páginas temporárias são liberadas antes de retornar ao processo, inclusive em caso de falha. No QEMU com 128 MiB, uma montagem de cerca de 60 kB levou aproximadamente 2,85 s entre pressionar Enter e observar a conclusão no Terminal.

## Testar no QEMU

Teste em uma cópia gravável da imagem. A build inclui `ASMTEST.APP` e seu fonte [ASMTEST.asm](../apps/ASMTEST.asm). Para recompilar o exemplo dentro do sistema, use `asm -f v1.1 ASMTEST.ASM ASMTEST.APP`.

```text
run ASMTEST.APP ola.asm OLA11.APP | cat
appinfo OLA11.APP
run OLA11.APP | cat
run ASMTEST.APP ola.asm OLAV1.APP v1 | cat
```

O teste usa v1.1 por padrão e aceita `v1` ou `v1.1` como terceiro argumento. Se houver erro com linha, o aplicativo a imprime e sai com código 1.
