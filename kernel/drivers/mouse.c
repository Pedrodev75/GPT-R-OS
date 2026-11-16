#include "mouse.h"
#include "vga_graphics.h"

enum { DATA = 0x60, STATUS = 0x64, PIC_MASTER = 0x20, PIC_SLAVE = 0xa0,
       OUTPUT_FULL = 1, INPUT_FULL = 2, AUXILIARY = 0x20, WAIT_LIMIT = 100000,
       QUEUE_MASK = 63 };

static unsigned char in(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static int send(unsigned short port, unsigned char value)
{
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i) {
        if (!(in(STATUS) & INPUT_FULL)) {
            out(port, value);
            return 1;
        }
    }
    return 0;
}

static int response(int auxiliary)
{
    for (unsigned int i = 0; i < WAIT_LIMIT; ++i) {
        const unsigned char status = in(STATUS);
        if (status == 0xff)
            break;
        if (status & OUTPUT_FULL) {
            const unsigned char value = in(DATA);
            if (!(status & 0xc0) && !!(status & AUXILIARY) == auxiliary)
                return value;
        }
    }
    return -1;
}

static int device_command(unsigned char command)
{
    for (unsigned int attempt = 0; attempt < 2; ++attempt) {
        if (!send(STATUS, 0xd4) || !send(DATA, command))
            return 0;
        const int reply = response(1);
        if (reply != 0xfe)
            return reply == 0xfa;
    }
    return 0;
}

static volatile unsigned int head, tail;
static volatile struct mouse_event queue[QUEUE_MASK + 1];
static unsigned int packet_count;
static unsigned char packet[3];
static int ready;
static struct mouse_event position;

int mouse_init(void)
{
    head = tail = packet_count = 0;
    position = (struct mouse_event){VGA_GRAPHICS_WIDTH / 2, VGA_GRAPHICS_HEIGHT / 2, 0};
    ready = 0;
    /* Keep keyboard bytes out of the controller's command response stream. */
    if (!send(STATUS, 0xad))
        return 0;
    for (unsigned int i = 0; i < 32 && (in(STATUS) & OUTPUT_FULL); ++i)
        (void)in(DATA);
    if (send(STATUS, 0xa8) && send(STATUS, 0x20)) {
        const int config = response(0);
        if (config >= 0 && device_command(0xf6) && device_command(0xf4) &&
            send(STATUS, 0x60) && send(DATA, (config | 2) & ~0x20))
            ready = 1;
    }
    if (!ready)
        send(STATUS, 0xa7);
    send(STATUS, 0xae);
    if (ready) {
        out(PIC_SLAVE + 1, in(PIC_SLAVE + 1) & ~0x10);
        out(PIC_MASTER + 1, in(PIC_MASTER + 1) & ~0x04);
    }
    return ready;
}

static unsigned short clamp(int value, int limit)
{
    return value < 0 ? 0 : value >= limit ? limit - 1 : value;
}

static void receive(unsigned char byte)
{
    if (!packet_count && !(byte & 8))
        return;
    packet[packet_count++] = byte;
    if (packet_count != 3)
        return;
    packet_count = 0;
    if (!(packet[0] & 0xc0)) {
        const int dx = (int)packet[1] - (packet[0] & 0x10 ? 256 : 0);
        const int dy = (int)packet[2] - (packet[0] & 0x20 ? 256 : 0);
        position.x = clamp((int)position.x + dx, VGA_GRAPHICS_WIDTH);
        position.y = clamp((int)position.y - dy, VGA_GRAPHICS_HEIGHT);
    }
    position.buttons = packet[0] & 7;
    const unsigned int next = (head + 1) & QUEUE_MASK;
    if (next == tail)
        queue[(head - 1) & QUEUE_MASK] = position;
    else {
        queue[head] = position;
        head = next;
    }
}

void mouse_irq_handler(void)
{
    const unsigned char status = in(STATUS);
    if ((status & (OUTPUT_FULL | AUXILIARY)) == (OUTPUT_FULL | AUXILIARY)) {
        const unsigned char byte = in(DATA);
        if (ready && !(status & 0xc0))
            receive(byte);
        else
            packet_count = 0;
    }
    out(PIC_SLAVE, 0x20);
    out(PIC_MASTER, 0x20);
}

/* Foreground callers poll with interrupts masked. */
int mouse_poll(struct mouse_event *event)
{
    if (head == tail)
        return 0;
    *event = queue[tail];
    tail = (tail + 1) & QUEUE_MASK;
    /* Paint the most recent motion, while retaining button transitions. */
    while (tail != head && queue[tail].buttons == event->buttons) {
        *event = queue[tail];
        tail = (tail + 1) & QUEUE_MASK;
    }
    return 1;
}

int mouse_available(void)
{
    return ready;
}
