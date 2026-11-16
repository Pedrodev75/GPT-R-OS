#include "shell_internal.h"
#include "../memory/heap.h"
#include "../memory/pages.h"

void shell_mem(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    const volatile struct e820_map *const map =
        E820_MAP;
    const unsigned int count = map->count;
    if (map->status == 0 || count > E820_MAX_ENTRIES || !count) {
        shell_print_error(row, "Mapa E820 indisponivel");
        return;
    }
    if (map->status != E820_COMPLETE)
        shell_print_error(row, "Mapa E820 incompleto");

    shell_print_line(row, "Base fisica        Tamanho (bytes)    Tipo E820");
    unsigned long long usable = 0;
    for (unsigned int i = 0; i < count; ++i) {
        const volatile struct e820_entry *entry = &map->entries[i];
        const unsigned long long length = entry->length;
        const unsigned int type = entry->type;
        char line[] = "0x0000000000000000 0x0000000000000000 0x0000000000000000";
        shell_hex64(line + 2, entry->base);
        shell_hex64(line + 21, length);
        shell_hex64(line + 40, type);
        shell_print_line(row, line);
        if (type == E820_USABLE)
            usable += length;
    }
    shell_print_line(row, "Tipo 1 = RAM utilizavel segundo a BIOS");
    shell_print_number(row, "RAM utilizavel (E820): ", usable / 1024, " KiB");
    shell_print_number(row, "RAM mapeada para alocacao: ",
                 pages_managed_count() * (PAGE_SIZE / 1024), " KiB");
    shell_print_number(row, "Paginas livres de 4 KiB: ", pages_free_count(), "");
}

static void show_address(unsigned int *row, unsigned long long address)
{
    char line[] = "Endereco: 0x0000000000000000";
    shell_hex64(line + 12, address);
    shell_print_line(row, line);
}

void shell_memtest(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    volatile unsigned char *page = page_alloc();
    if (!page) {
        shell_print_line(row, "memtest: nenhuma pagina livre");
        return;
    }
    show_address(row, (unsigned long long)page);
    static const unsigned char patterns[] = {0xaa, 0x55};
    unsigned int ok = 1;
    for (unsigned int p = 0; p < sizeof(patterns) && ok; ++p) {
        for (unsigned int i = 0; i < PAGE_SIZE; ++i)
            page[i] = patterns[p];
        for (unsigned int i = 0; i < PAGE_SIZE; ++i) {
            if (page[i] != patterns[p]) {
                shell_print_number(row, "memtest: falha no byte ", i, "");
                ok = 0;
                break;
            }
        }
    }
    if (!page_free((void *)page))
        shell_print_line(row, "memtest: falha ao liberar pagina");
    else if (ok)
        shell_print_line(row, "memtest: 4 KiB OK (0xAA e 0x55). Pagina liberada.");
}

void shell_peek(unsigned int *row, const unsigned char *argument)
{
    enum { BYTE_COUNT = 16 };
    unsigned long long address;
    if (!shell_parse_address(argument, &address)) {
        shell_print_line(row, "Uso: peek <endereco hexadecimal>");
        return;
    }
    if (!pages_contains_range(address, BYTE_COUNT)) {
        shell_print_line(row, "peek: faixa fora da RAM gerenciada");
        return;
    }
    show_address(row, address);
    const volatile unsigned char *bytes = (const volatile unsigned char *)address;
    shell_print_bytes(row, bytes);
}

enum { SHELL_HEAP_SLOTS = 32 };
static void *shell_blocks[SHELL_HEAP_SLOTS];

void shell_kmalloc(unsigned int *row, const unsigned char *argument)
{
    unsigned int bytes;
    if (shell_parse_decimal(argument, &bytes) != 1 || !bytes) {
        shell_print_line(row, "Uso: kmalloc <bytes em decimal>");
        return;
    }
    for (unsigned int i = 0; i < SHELL_HEAP_SLOTS; ++i) {
        if (shell_blocks[i])
            continue;
        void *block = kmalloc(bytes);
        if (!block) {
            shell_print_line(row, "kmalloc: sem espaco para esse bloco");
            return;
        }
        shell_blocks[i] = block;
        show_address(row, (unsigned long long)block);
        return;
    }
    shell_print_line(row, "kmalloc: limite de 32 blocos ativos no shell");
}

void shell_kfree(unsigned int *row, const unsigned char *argument)
{
    unsigned long long address;
    if (!shell_parse_address(argument, &address)) {
        shell_print_line(row, "Uso: kfree <endereco hexadecimal>");
        return;
    }
    for (unsigned int i = 0; i < SHELL_HEAP_SLOTS; ++i) {
        void *block = shell_blocks[i];
        if ((unsigned long long)block != address || !block)
            continue;
        shell_blocks[i] = 0;
        kfree(block);
        shell_print_line(row, "Bloco liberado");
        return;
    }
    shell_print_line(row, "kfree: bloco ativo nao encontrado no shell");
}

void shell_memory_init(void)
{
    for (unsigned int i = 0; i < SHELL_HEAP_SLOTS; ++i)
        shell_blocks[i] = 0;
}
