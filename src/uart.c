#include "uart.h"
#include <stdio.h>
#include <avr/io.h>
#include <util/setbaud.h>

int uart_putchar(char c, FILE* f) {
  if (c == '\n') uart_putchar('\r', f);
  loop_until_bit_is_set(UCSR0A, UDRE0); // Wait until data register empty.
  UDR0 = c;
  return 0;
}
int uart_getchar(FILE* f) {
  loop_until_bit_is_set(UCSR0A, RXC0); // Wait until data exists.
  return UDR0;
}

static FILE uart_output = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
static FILE uart_input = FDEV_SETUP_STREAM(NULL, uart_getchar, _FDEV_SETUP_READ);

void uart_init(void) {
  UBRR0H = UBRRH_VALUE;
  UBRR0L = UBRRL_VALUE;

#if USE_2X
  UCSR0A |= _BV(U2X0);
#else
  UCSR0A &= ~(_BV(U2X0));
#endif

  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); // 8-bit data
  UCSR0B = _BV(RXEN0) | _BV(TXEN0);   // Enable RX and TX

  stdout = &uart_output;
  stdin  = &uart_input;
}

