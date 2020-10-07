/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <kern/kclock.h>

void
rtc_init(void) {
  nmi_disable();
  // LAB 4: Your code here
  uint8_t B;
  // Switch to clock register B.
  outb(IO_RTC_CMND, RTC_BREG);
  // Read the value of register B from the I / O port.
  B = inb(IO_RTC_DATA);
  // Setting the RTC_PIE + bit Write the updated register value to the I / O port.
  outb(IO_RTC_DATA, B | RTC_PIE);
  outb(IO_RTC_CMND, RTC_AREG);
  // Сhanging the frequency divider, which corresponds to the lower 4 bits of the clock register A.
  uint8_t A = inb(IO_RTC_DATA);
  outb(IO_RTC_DATA, A | 0xF);

  nmi_enable();
}

uint8_t
rtc_check_status(void) {
  uint8_t status = 0;
  // LAB 4: Your code here
	outb(IO_RTC_CMND, RTC_CREG);
  // Read the value of the clock register C.
	status = inb(IO_RTC_DATA);
  return status;
}
