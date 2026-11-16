#include "shell_internal.h"
#include "../drivers/keyboard.h"

#ifdef GPT_R_GRAPHICS

enum {
    TEXT_LIMIT = 4096,
    VIEW_X = 61, VIEW_Y = 51, ROW_HEIGHT = 17, VIEW_ROWS = 22, VIEW_COLS = 55,
    CELL = VGA_OVERLAY_CELL_WIDTH, FOOTER_Y = 429, ROW_RIGHT = 620,
    DIALOG_TOP = 161, DIALOG_HEIGHT = 112,
    BACKGROUND = 0x0b1423, PANEL = 0x142338, ACTIVE = 0x1a2c45,
    BORDER = 0x2e415c, LABEL = 0x9bacc4, TEXT = 0xe7eef8, ACCENT = 0x61dcf5,
    MUTED = 0x8095ac, UNSAVED = 0xffcb82
};

struct editor {
    unsigned int length, cursor, top, left, goal;
    int use_goal, changed, exists, confirming;
    const char *notice;
};

static unsigned char document[TEXT_LIMIT];

static unsigned int decode(const unsigned char *text, unsigned int available,
    unsigned int *codepoint)
{
    if (!available)
        return 0;
    const unsigned int lead = text[0];
    unsigned int size = 1, minimum = 0;
    *codepoint = lead;
    if (lead >= 0xc2 && lead < 0xe0) {
        size = 2;
        minimum = 0x80;
        *codepoint &= 0x1f;
    } else if (lead >= 0xe0 && lead < 0xf0) {
        size = 3;
        minimum = 0x800;
        *codepoint &= 0x0f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        size = 4;
        minimum = 0x10000;
        *codepoint &= 7;
    } else if (lead >= 0x80) {
        return 0;
    }
    if (size > available)
        return 0;
    for (unsigned int i = 1; i < size; ++i) {
        if ((text[i] & 0xc0) != 0x80)
            return 0;
        *codepoint = (*codepoint << 6) | (text[i] & 0x3f);
    }
    if (*codepoint < minimum || *codepoint > 0x10ffff ||
        (*codepoint >= 0xd800 && *codepoint <= 0xdfff))
        return 0;
    return size;
}

static int load_chunk(void *context, const unsigned char *data, unsigned int size)
{
    struct editor *edit = context;
    if (size > sizeof(document) - edit->length)
        return 0;
    for (unsigned int i = 0; i < size; ++i)
        document[edit->length++] = data[i];
    return 1;
}

static int normalize_text(struct editor *edit)
{
    unsigned int output = 0;
    for (unsigned int i = 0; i < edit->length;) {
        unsigned int cp;
        const unsigned int size = decode(document + i, edit->length - i, &cp);
        if (!size || (cp < 32 && cp != '\r' && cp != '\n' && cp != '\t') || cp == 127)
            return 0;
        if (cp == '\r') {
            document[output++] = '\n';
            ++i;
            if (i < edit->length && document[i] == '\n')
                ++i;
        } else {
            for (unsigned int j = 0; j < size; ++j)
                document[output++] = document[i++];
        }
    }
    edit->length = output;
    return 1;
}

static unsigned int after_character(const struct editor *edit, unsigned int at)
{
    if (at < edit->length) {
        ++at;
        while (at < edit->length && (document[at] & 0xc0) == 0x80)
            ++at;
    }
    return at;
}

static unsigned int before_character(unsigned int at)
{
    if (at) {
        --at;
        while (at && (document[at] & 0xc0) == 0x80)
            --at;
    }
    return at;
}

static unsigned int start_of_line(unsigned int at)
{
    while (at && document[at - 1] != '\n')
        --at;
    return at;
}

static unsigned int end_of_line(const struct editor *edit, unsigned int at)
{
    while (at < edit->length && document[at] != '\n')
        ++at;
    return at;
}

static unsigned int glyph_width(unsigned int column, unsigned char byte)
{
    return byte == '\t' ? 4 - column % 4 : 1;
}

static unsigned int column_at(const struct editor *edit, unsigned int at)
{
    unsigned int column = 0;
    for (unsigned int i = start_of_line(at); i < at; i = after_character(edit, i))
        column += glyph_width(column, document[i]);
    return column;
}

static void move_vertical(struct editor *edit, int down)
{
    if (!edit->use_goal)
        edit->goal = column_at(edit, edit->cursor);
    edit->use_goal = 1;
    unsigned int target = start_of_line(edit->cursor);
    if (down) {
        target = end_of_line(edit, target);
        if (target == edit->length)
            return;
        ++target;
    } else {
        if (!target)
            return;
        target = start_of_line(target - 1);
    }
    unsigned int column = 0;
    while (target < edit->length && document[target] != '\n') {
        const unsigned int next = column + glyph_width(column, document[target]);
        if (next > edit->goal)
            break;
        column = next;
        target = after_character(edit, target);
    }
    edit->cursor = target;
}

static void mark_changed(struct editor *edit)
{
    edit->changed = 1;
    edit->use_goal = 0;
    edit->notice = 0;
}

static void remove_text(struct editor *edit, unsigned int first, unsigned int last)
{
    if (first == last)
        return;
    for (unsigned int i = last; i < edit->length; ++i)
        document[first + i - last] = document[i];
    edit->length -= last - first;
    edit->cursor = first;
    mark_changed(edit);
}

static void insert_text(struct editor *edit, const unsigned char *text, unsigned int size)
{
    if (size > sizeof(document) - edit->length) {
        edit->notice = "Limite de 4 KiB";
        return;
    }
    for (unsigned int i = edit->length; i > edit->cursor; --i)
        document[i + size - 1] = document[i - 1];
    for (unsigned int i = 0; i < size; ++i)
        document[edit->cursor++] = text[i];
    edit->length += size;
    mark_changed(edit);
}

static void apply_key(struct editor *edit, unsigned int key)
{
    if (key == KEY_UP || key == KEY_DOWN) {
        move_vertical(edit, key == KEY_DOWN);
        return;
    }
    edit->use_goal = 0;
    switch (key) {
    case KEY_LEFT:
        edit->cursor = before_character(edit->cursor);
        break;
    case KEY_RIGHT:
        edit->cursor = after_character(edit, edit->cursor);
        break;
    case KEY_HOME:
        edit->cursor = start_of_line(edit->cursor);
        break;
    case KEY_END:
        edit->cursor = end_of_line(edit, edit->cursor);
        break;
    case '\b':
        remove_text(edit, before_character(edit->cursor), edit->cursor);
        break;
    case KEY_DELETE:
        remove_text(edit, edit->cursor, after_character(edit, edit->cursor));
        break;
    case '\n': {
        const unsigned char newline = '\n';
        insert_text(edit, &newline, 1);
        break;
    }
    case '\t': {
        const unsigned char blanks[] = "    ";
        insert_text(edit, blanks, glyph_width(column_at(edit, edit->cursor), '\t'));
        break;
    }
    default:
        if (key >= ' ' && key <= 255) {
            unsigned char bytes[3];
            const unsigned int size = shell_encode_glyph(bytes, key);
            insert_text(edit, bytes, size);
        }
        break;
    }
}

static void label(unsigned int x, unsigned int y, const char *text, unsigned int rgb)
{
    for (; *text; ++text, x += CELL)
        vga_graphics_overlay_glyph(x, y, (unsigned char)*text, rgb);
}

static void right_number(unsigned int x, unsigned int y, unsigned int number, unsigned int rgb)
{
    do {
        x -= CELL;
        vga_graphics_overlay_glyph(x, y, '0' + number % 10, rgb);
        number /= 10;
    } while (number);
}

static unsigned int line_by_number(unsigned int row, unsigned int length)
{
    for (unsigned int i = 0; i < length; ++i)
        if (document[i] == '\n' && !--row)
            return i + 1;
    return 0;
}

struct editor_row {
    unsigned char glyphs[VIEW_COLS];
    unsigned int number, ink;
    int active, caret;
};

static struct {
    struct editor_row rows[VIEW_ROWS];
    unsigned int line, column, scroll_y, scroll_size;
    const char *notice;
    int initialized, status, confirming;
} view;

static void paint_row(unsigned int index, const struct editor_row *next, int force)
{
    struct editor_row *old = &view.rows[index];
    const unsigned int y = VIEW_Y + index * ROW_HEIGHT;
    const unsigned int background = next->active ? ACTIVE : BACKGROUND;
    const unsigned int gutter = next->active ? ACTIVE : PANEL;
    const int repaint_background = force || next->active != old->active;
    if (repaint_background) {
        vga_graphics_fill(0, y, VIEW_X - 8, ROW_HEIGHT, gutter);
        vga_graphics_fill(VIEW_X - 8, y, 8, ROW_HEIGHT, background);
        const unsigned int text_right = VIEW_X + CELL * VIEW_COLS;
        vga_graphics_fill(text_right, y, ROW_RIGHT - text_right, ROW_HEIGHT, background);
    }
    if (repaint_background || next->number != old->number) {
        if (!repaint_background)
            vga_graphics_fill(4, y, 40, ROW_HEIGHT, gutter);
        if (next->number)
            right_number(44, y, next->number, next->active ? ACCENT : MUTED);
    }
    for (unsigned int col = 0; col < VIEW_COLS; ++col) {
        const unsigned char glyph = next->glyphs[col];
        const int caret = (int)col == next->caret;
        if (!repaint_background && glyph == old->glyphs[col] &&
            (!glyph || next->ink == old->ink) && caret == ((int)col == old->caret))
            continue;

        const unsigned int x = VIEW_X + col * CELL;
        if (!glyph || glyph == ' ') {
            vga_graphics_fill(x, y, CELL, ROW_HEIGHT, background);
        } else {
            vga_graphics_overlay_cell(x, y, glyph, next->ink, background);
            vga_graphics_fill(x, y + VGA_OVERLAY_GLYPH_HEIGHT, CELL,
                ROW_HEIGHT - VGA_OVERLAY_GLYPH_HEIGHT, background);
        }
        if (caret)
            vga_graphics_fill(x, y + 1, 2, ROW_HEIGHT - 2, ACCENT);
    }
    *old = *next;
}

static void draw_body(struct editor *edit, unsigned int active_row, unsigned int active_col,
    unsigned int lines, int assembly)
{
    unsigned int at = edit->top ? line_by_number(edit->top, edit->length) : 0;
    for (unsigned int visible = 0; visible < VIEW_ROWS; ++visible) {
        struct editor_row next = { .caret = -1 };
        const unsigned int row = visible + edit->top;
        if (row < lines) {
            next.number = row + 1;
            next.active = row == active_row;
            if (next.active)
                next.caret = active_col - edit->left;
            unsigned int col = 0;
            const unsigned int end = end_of_line(edit, at);
            unsigned int first = at;
            while (first < end && (document[first] == ' ' || document[first] == '\t'))
                ++first;
            next.ink = assembly && first < end && document[first] == ';' ? MUTED : TEXT;
            while (at < end && col < edit->left + VIEW_COLS) {
                unsigned int cp;
                const unsigned int bytes = decode(document + at, end - at, &cp);
                if (!bytes)
                    break;
                if (col >= edit->left && cp != '\t')
                    next.glyphs[col - edit->left] = shell_codepoint_glyph(cp);
                col += glyph_width(col, document[at]);
                at += bytes;
            }
            at = end + 1;
        }
        const unsigned int y = VIEW_Y + visible * ROW_HEIGHT;
        const int covered = view.confirming && !edit->confirming &&
            y < DIALOG_TOP + DIALOG_HEIGHT && y + ROW_HEIGHT > DIALOG_TOP;
        paint_row(visible, &next, !view.initialized || covered);
    }
}

static void draw_scrollbar(unsigned int top, unsigned int lines)
{
    const unsigned int height = VIEW_ROWS * ROW_HEIGHT;
    unsigned int thumb = 0, y = VIEW_Y;
    if (lines > VIEW_ROWS) {
        thumb = height * VIEW_ROWS / lines;
        if (thumb < 9)
            thumb = 9;
        y += (height - thumb) * top / (lines - VIEW_ROWS);
    }
    if (!view.initialized || y != view.scroll_y || thumb != view.scroll_size) {
        vga_graphics_fill(629, VIEW_Y, 3, height, thumb ? BORDER : BACKGROUND);
        if (thumb)
            vga_graphics_fill(629, y, 3, thumb, MUTED);
        view.scroll_y = y;
        view.scroll_size = thumb;
    }
}

static void draw_frame(const unsigned char *name)
{
    vga_graphics_fill(0, 0, VGA_GRAPHICS_WIDTH, VGA_GRAPHICS_HEIGHT, BACKGROUND);
    vga_graphics_fill(0, 0, VGA_GRAPHICS_WIDTH, 46, PANEL);
    vga_graphics_fill(0, 45, VGA_GRAPHICS_WIDTH, 1, BORDER);
    vga_graphics_fill(0, 0, 4, 46, ACCENT);
    label(16, 14, "EDITOR", LABEL);
    label(96, 14, (const char *)name, TEXT);
    vga_graphics_fill(0, FOOTER_Y, VGA_GRAPHICS_WIDTH, VGA_GRAPHICS_HEIGHT - FOOTER_Y, PANEL);
    vga_graphics_fill(0, FOOTER_Y, VGA_GRAPHICS_WIDTH, 1, BORDER);
    label(13, 436, "Ln", LABEL);
    label(101, 436, "Col", LABEL);
    label(568, 436, "UTF-8", MUTED);
    label(13, 459, "Ctrl+S Salvar    Esc Sair    Setas Mover", LABEL);
}

static void draw_status(const struct editor *edit, unsigned int line, unsigned int col)
{
    const int status = edit->changed ? 2 : edit->exists ? 1 : 0;
    if (!view.initialized || status != view.status) {
        vga_graphics_fill(532, 10, 94, 25, ACTIVE);
        label(543, 14, status == 2 ? "EDITADO" : status == 1 ? "SALVO" : "NOVO",
            status == 2 ? UNSAVED : ACCENT);
        view.status = status;
    }
    if (!view.initialized || line != view.line) {
        vga_graphics_fill(45, 436, 40, VGA_OVERLAY_GLYPH_HEIGHT, PANEL);
        right_number(85, 436, line + 1, TEXT);
        view.line = line;
    }
    if (!view.initialized || col != view.column) {
        vga_graphics_fill(130, 436, 50, VGA_OVERLAY_GLYPH_HEIGHT, PANEL);
        right_number(180, 436, col + 1, TEXT);
        view.column = col;
    }
    if (!view.initialized || edit->notice != view.notice) {
        vga_graphics_fill(204, 436, 350, VGA_OVERLAY_GLYPH_HEIGHT, PANEL);
        if (edit->notice)
            label(204, 436, edit->notice, UNSAVED);
        view.notice = edit->notice;
    }
}

static void draw_editor(struct editor *edit, const unsigned char *name)
{
    unsigned int line = 0, lines = 1;
    for (unsigned int i = 0; i < edit->length; ++i) {
        if (document[i] == '\n') {
            ++lines;
            if (i < edit->cursor)
                ++line;
        }
    }
    const unsigned int col = column_at(edit, edit->cursor);
    if (line < edit->top)
        edit->top = line;
    else if (line >= edit->top + VIEW_ROWS)
        edit->top = line - VIEW_ROWS + 1;
    if (edit->top && edit->top + VIEW_ROWS > lines)
        edit->top = lines > VIEW_ROWS ? lines - VIEW_ROWS : 0;
    if (col < edit->left)
        edit->left = col;
    else if (col >= edit->left + VIEW_COLS)
        edit->left = col - VIEW_COLS + 1;

    if (!view.initialized)
        draw_frame(name);
    unsigned int name_length = 0;
    while (name[name_length])
        ++name_length;
    const int assembly = name_length > 4 && shell_equals(name + name_length - 4, ".asm");
    draw_body(edit, line, col, lines, assembly);
    draw_scrollbar(edit->top, lines);
    draw_status(edit, line, col);

    if (edit->confirming && !view.confirming) {
        vga_graphics_fill(106, DIALOG_TOP, 428, DIALOG_HEIGHT, BORDER);
        vga_graphics_fill(108, DIALOG_TOP + 2, 424, DIALOG_HEIGHT - 4, PANEL);
        vga_graphics_fill(108, DIALOG_TOP + 2, 424, 3, UNSAVED);
        label(126, 181, "Alteracoes sem salvar", UNSAVED);
        label(126, 211, "S Salvar e sair    D Descartar", TEXT);
        label(126, 238, "Esc Continuar editando", LABEL);
    }
    view.confirming = edit->confirming;
    view.initialized = 1;
}

static int save_file(struct editor *edit, const struct fat16_volume *volume,
    const unsigned char *name, unsigned int name_size)
{
    const enum fat16_edit_result result =
        fat16_write(volume, name, name_size, document, edit->length);
    if (result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP) {
        edit->changed = 0;
        edit->exists = 1;
        edit->notice = result == FAT16_EDIT_OK ? "Arquivo salvo" : "Salvo; falha na limpeza do FAT";
        return 1;
    }
    switch (result) {
    case FAT16_EDIT_TEXT_EXTENSION:
        edit->notice = "Use a extensao .txt";
        break;
    case FAT16_EDIT_NAME:
        edit->notice = "Use um nome 8.3 valido";
        break;
    case FAT16_EDIT_CASE:
        edit->notice = "Nome com outra capitalizacao";
        break;
    case FAT16_EDIT_FULL:
        edit->notice = "Disco ou diretorio cheio";
        break;
    case FAT16_EDIT_PROTECTED:
        edit->notice = "Arquivo protegido";
        break;
    default:
        edit->notice = "Falha ao salvar no FAT16";
        break;
    }
    return 0;
}

void shell_edit(unsigned int *row, const unsigned char *argument)
{
    const unsigned char *name = shell_skip_spaces(argument);
    unsigned int name_size = 0;
    while (name[name_size] && name[name_size] != ' ')
        ++name_size;
    if (!name_size || name_size > 12 || name[name_size]) {
        shell_print_line(row, "Uso: edit <arquivo>");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    if (!shell_open_volume(row, &disk, &volume))
        return;
    const int found = shell_lookup_file(&volume, name, &file);
    if (found < 0) {
        shell_print_line(row, "Falha ao ler diretorio");
        return;
    }
    struct editor edit = { .exists = found };
    if (found) {
        if (file.size > sizeof(document)) {
            shell_print_line(row, "Limite do editor: 4 KiB");
            return;
        }
        if (fat16_read(&volume, &file, load_chunk, &edit) != FAT16_READ_OK) {
            shell_print_line(row, "Falha ao ler arquivo");
            return;
        }
        if (!normalize_text(&edit)) {
            shell_print_line(row, "O editor precisa de um arquivo de texto UTF-8");
            return;
        }
    }

    view.initialized = view.confirming = 0;
    vga_graphics_begin_overlay();
    for (;;) {
        draw_editor(&edit, name);
        const unsigned int key = shell_wait_key();
        if (edit.confirming) {
            if (key == 'd' || key == 'D')
                break;
            if (key == KEY_ESCAPE) {
                edit.confirming = 0;
            } else if (key == KEY_SAVE || key == 's' || key == 'S') {
                if (save_file(&edit, &volume, name, name_size))
                    break;
                edit.confirming = 0;
            }
        } else if (key == KEY_ESCAPE) {
            if (!edit.changed)
                break;
            edit.confirming = 1;
        } else if (key == KEY_SAVE) {
            save_file(&edit, &volume, name, name_size);
        } else {
            apply_key(&edit, key);
        }
    }
    vga_graphics_restore_console();
}

#endif
