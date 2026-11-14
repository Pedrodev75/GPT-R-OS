void kernel_main(void)
{
    volatile unsigned short *const vga = (volatile unsigned short *)0xb8000;
    const char message[] = "Kernel iniciado em x86-64";

    for (unsigned int i = 0; message[i] != '\0'; ++i) {
        vga[i] = 0x0f00 | (unsigned char)message[i];
    }
}
