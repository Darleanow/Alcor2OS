/**
 * @file src/kernel/ktermios.c
 * @brief Default termios (canonical + echo) for new processes.
 */

#include <alcor2/kstdlib.h>
#include <alcor2/ktermios.h>

void ktermios_init_default(k_termios_t *t)
{
  kzero(t, sizeof(*t));
  t->c_iflag           = KTERM_ICRNL;
  t->c_oflag           = KTERM_ONLCR;
  t->c_cflag           = KTERM_CS8 | KTERM_CREAD | KTERM_CLOCAL;
  t->c_lflag           = KTERM_ISIG | KTERM_ICANON | KTERM_ECHO | KTERM_IEXTEN;
  t->__c_ispeed        = KTERM_B38400;
  t->__c_ospeed        = KTERM_B38400;
  t->c_cc[0]           = '\x03'; /* VINTR */
  t->c_cc[1]           = 0x1c;   /* VQUIT */
  t->c_cc[2]           = 0x7f;   /* VERASE */
  t->c_cc[3]           = 0x15;   /* VKILL */
  t->c_cc[4]           = '\x04'; /* VEOF */
  t->c_cc[KTERM_VMIN]  = 1;
  t->c_cc[KTERM_VTIME] = 0;
}
