/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "usb_dev.h"
#if F_CPU >= 20000000 && defined(NUM_ENDPOINTS)

#include "kinetis.h"
//#include "HardwareSerial.h"
#include "usb_mem.h"

__attribute__ ((section(".usbbuffers"), used))
unsigned char usb_buffer_memory[NUM_USB_BUFFERS * sizeof(usb_packet_t)];

static uint32_t usb_buffer_available = 0xFFFFFFFF;

#if defined(MRCC_USB_MIDI12_SERIAL)
// MRCC: receive endpoints may not take the last USB_TX_RESERVE buffers.
// Otherwise unread received packets (e.g. a SysEx dump from the PC routed to a
// slow 5-pin port) fill the whole pool, usb_midi_write_packed finds no buffer,
// and after its 40 ms timeout it drops outgoing MIDI. With the reserve, receive
// simply stops (the host gets NAKs = flow control) and transmit keeps working.
#define USB_TX_RESERVE 4
static volatile uint8_t usb_buffers_free = NUM_USB_BUFFERS;
#endif

// use bitmask and CLZ instruction to implement fast free list
// http://www.archivum.info/gnu.gcc.help/2006-08/00148/Re-GCC-Inline-Assembly.html
// http://gcc.gnu.org/ml/gcc/2012-06/msg00015.html
// __builtin_clz()

#if defined(MRCC_USB_MIDI12_SERIAL)
static usb_packet_t * usb_malloc_keep(unsigned int keep_free)
#else
usb_packet_t * usb_malloc(void)
#endif
{
	unsigned int n, avail;
	uint8_t *p;

	__disable_irq();
#if defined(MRCC_USB_MIDI12_SERIAL)
	if (usb_buffers_free <= keep_free) {
		__enable_irq();
		return NULL;
	}
#endif
	avail = usb_buffer_available;
	n = __builtin_clz(avail); // clz = count leading zeros
	if (n >= NUM_USB_BUFFERS) {
		__enable_irq();
		return NULL;
	}
	//serial_print("malloc:");
	//serial_phex(n);
	//serial_print("\n");

	usb_buffer_available = avail & ~(0x80000000 >> n);
#if defined(MRCC_USB_MIDI12_SERIAL)
	usb_buffers_free--;
#endif
	__enable_irq();
	p = usb_buffer_memory + (n * sizeof(usb_packet_t));
	//serial_print("malloc:");
	//serial_phex32((int)p);
	//serial_print("\n");
	*(uint32_t *)p = 0;
	*(uint32_t *)(p + 4) = 0;
	return (usb_packet_t *)p;
}

#if defined(MRCC_USB_MIDI12_SERIAL)
usb_packet_t * usb_malloc(void)
{
	return usb_malloc_keep(0);
}

// For re-arming receive endpoints: fails while only the TX reserve is left.
usb_packet_t * usb_malloc_rx(void)
{
	return usb_malloc_keep(USB_TX_RESERVE);
}
#endif

// for the receive endpoints to request memory
extern uint8_t usb_rx_memory_needed;
extern void usb_rx_memory(usb_packet_t *packet);

void usb_free(usb_packet_t *p)
{
	unsigned int n, mask;

	//serial_print("free:");
	n = ((uint8_t *)p - usb_buffer_memory) / sizeof(usb_packet_t);
	if (n >= NUM_USB_BUFFERS) return;
	//serial_phex(n);
	//serial_print("\n");

	// if any endpoints are starving for memory to receive
	// packets, give this memory to them immediately!
	// MRCC: ...unless that would eat into the TX reserve. Then the buffer goes back
	// to the pool, and a later free hands one to the starving endpoint.
#if defined(MRCC_USB_MIDI12_SERIAL)
	if (usb_rx_memory_needed && usb_configuration && usb_buffers_free >= USB_TX_RESERVE) {
#else
	if (usb_rx_memory_needed && usb_configuration) {
#endif
		//serial_print("give to rx:");
		//serial_phex32((int)p);
		//serial_print("\n");
		usb_rx_memory(p);
		return;
	}

	mask = (0x80000000 >> n);
	__disable_irq();
	usb_buffer_available |= mask;
#if defined(MRCC_USB_MIDI12_SERIAL)
	usb_buffers_free++;
#endif
	__enable_irq();

	//serial_print("free:");
	//serial_phex32((int)p);
	//serial_print("\n");
}

#endif // F_CPU >= 20 MHz && defined(NUM_ENDPOINTS)
