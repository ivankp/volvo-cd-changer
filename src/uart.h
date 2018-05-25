#ifndef UART_HH
#define UART_HH

#define UART_RECEIVED (UCSR0A & (1<<RXC0))

void uart_init(void);

#endif
