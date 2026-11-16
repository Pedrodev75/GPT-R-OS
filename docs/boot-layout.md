# Layout de boot do GPT-R OS

O setor de boot carrega o bootstrap de 16 bits. O bootstrap lê o cabeçalho do
kernel, carrega o binário numa área temporária e entra em modo longo. Depois,
copia o kernel para 1 MiB. A entrada do kernel limpa a BSS e usa uma pilha de
64 KiB no topo dos primeiros 2 MiB. O alocador físico começa em 2 MiB.

| LBA | Conteúdo |
|---|---|
| 0 | Setor de boot e tabela de partições MBR. |
| 1–16 | Bootstrap da seção `.stage2`. |
| 17 | Cabeçalho de 512 bytes com assinatura `GTRK` e comprimento do kernel como inteiro de 32 bits little-endian. |
| 18 em diante | Kernel da seção `.kernel`, com entrada no primeiro byte. |
| 2048 | Início da partição FAT16 da imagem de exemplo. |

O buffer de leitura fica entre `0x10000` e `0x70000`, com capacidade de
384 KiB. A BIOS lê no máximo 64 setores por chamada EDD e cada transferência
fica dentro de um limite físico de 64 KiB.

## Empacotar

Depois de compilar e ligar com `boot/layout.ld`, extraia as três partes do
ELF com GNU objcopy:

```sh
objcopy -O binary -j .boot os.elf boot.bin
objcopy -O binary -j .stage2 os.elf loader.bin
objcopy -O binary -j .kernel os.elf kernel.bin
python3 tools/pack-image.py --boot boot.bin --loader loader.bin \
    --kernel kernel.bin --disk disco-base.img --output gpt-r-hd.img
```

Passe a imagem FAT16 já particionada em `--disk`. O empacotador mantém a
tabela de partições e o conteúdo das partições e grava uma nova imagem em
`--output`.
