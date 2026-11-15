#ifndef GPT_R_HEAP_H
#define GPT_R_HEAP_H

void heap_init(void);
/* Foreground kernel code only. Each allocation fits inside a single page. */
void *kmalloc(unsigned int size);
void kfree(void *pointer);
int heap_self_test(void);

#endif
