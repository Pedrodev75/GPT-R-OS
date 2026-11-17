#include "settings.h"

#ifdef GPT_R_GRAPHICS
#include "tool_view.h"
#include "../fs/fat16.h"

static const unsigned char prefs_name[] = "DESKTOP.DAT";
static struct {
    unsigned char green_theme, clock_12h;
    const char *notice;
} prefs;

static int config_folder(struct ata_disk *disk, struct fat16_volume *volume,
    unsigned int *directory)
{
    if (!ata_identify(disk) || !fat16_mount(volume, disk))
        return 0;
    struct fat16_file file;
    if (fat16_find_at(volume, 0, (const unsigned char *)"CONFIG", &file) != 1 ||
        !file.is_directory || !file.first_cluster)
        return 0;
    *directory = file.first_cluster;
    return 1;
}

void settings_init(void)
{
    prefs.green_theme = prefs.clock_12h = 0;
    prefs.notice = 0;
    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    unsigned int directory;
    unsigned char data[6];
    if (config_folder(&disk, &volume, &directory) &&
        fat16_lookup_at(&volume, directory, prefs_name, &file) == 1 &&
        file.size == sizeof(data) && fat16_read_range(&volume, &file, 0, data, sizeof(data)) &&
        data[0] == 'G' && data[1] == 'T' && data[2] == 'D' && data[3] == '1' &&
        data[4] <= 1 && data[5] <= 1) {
        prefs.green_theme = data[4];
        prefs.clock_12h = data[5];
    }
}

int settings_green_theme(void) { return prefs.green_theme; }
int settings_clock_12h(void) { return prefs.clock_12h; }

static int toggle(int clock)
{
    const unsigned char data[] = {
        'G', 'T', 'D', '1', prefs.green_theme ^ !clock, prefs.clock_12h ^ clock
    };
    struct ata_disk disk;
    struct fat16_volume volume;
    unsigned int directory;
    prefs.notice = "Falha ao salvar preferencias";
    if (!config_folder(&disk, &volume, &directory))
        return 1;
    const enum fat16_edit_result result = fat16_write_at(&volume, directory,
        prefs_name, sizeof(prefs_name) - 1, data, sizeof(data));
    if (result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP) {
        prefs.green_theme = data[4];
        prefs.clock_12h = data[5];
        prefs.notice = "Preferencias salvas";
    }
    return 1;
}

int settings_click(unsigned int x, unsigned int y)
{
    if (tool_hit(x, y, 324, 34, 198, 33))
        return toggle(0);
    if (tool_hit(x, y, 324, 101, 198, 33))
        return toggle(1);
    return 0;
}

int settings_key(unsigned int key)
{
    return key == '1' ? toggle(0) : key == '2' ? toggle(1) : 0;
}

void settings_draw(unsigned int x, unsigned int y)
{
    vga_graphics_fill(x, y, SETTINGS_WIDTH, SETTINGS_HEIGHT, TOOL_BG);
    tool_label(x + 24, y + 39, "1  Tema do desktop", TOOL_TEXT);
    tool_button(x + 324, y + 34, 198, prefs.green_theme ? "Verde" : "Azul", TOOL_BLUE, 1);
    tool_label(x + 24, y + 106, "2  Relogio", TOOL_TEXT);
    tool_button(x + 324, y + 101, 198, prefs.clock_12h ? "12 horas" : "24 horas", TOOL_BLUE, 1);
    if (prefs.notice)
        tool_label(x + 24, y + 181, prefs.notice, TOOL_MUTED);
}
#endif
