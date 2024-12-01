/* mac_command
 *
 * Sends commands to and controls "mac_fpga" project on the
 * Tang Nano 9K FPGA development board.
 *
 * License SPDX BSD-2-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <pigpio.h>

#define WIDTH 16
#define DELAY (200)

// OUT is from the point of view of the FPGA for pin names
#define CLK_PIN  21
#define CMD1_PIN 20
#define CMD0_PIN 16
#define DIN_PIN  26
#define DOUT_PIN 19

int all_input_pins[] = {DOUT_PIN};
int all_output_pins[] = {CLK_PIN, CMD1_PIN, CMD0_PIN, DIN_PIN};

uint16_t a_ref = 0;
uint16_t b_ref = 0;
uint32_t c_ref = 0;

void cleanup(void)
{
  int i;

  // Set all output pins back to input

  for (i = 0; i < sizeof(all_output_pins)/sizeof(all_output_pins[0]); i++)
    gpioSetMode(all_output_pins[i], PI_INPUT);

  gpioTerminate();
}

void setup_gpios(void)
{
  int i;

  if (gpioInitialise() == PI_INIT_FAILED) {
    fprintf(stderr, "Could not start pigpio\n");
    exit(EXIT_FAILURE);
  }
  
  for (i = 0; i < sizeof(all_output_pins)/sizeof(all_output_pins[0]); i++) {
    if (gpioSetMode(all_output_pins[i], PI_OUTPUT) != 0) {
      fprintf(stderr, "Could not set pin %d to output\n", i);
      exit(EXIT_FAILURE);
    }
    if (gpioSetPullUpDown(all_output_pins[i], PI_PUD_OFF) != 0) {
      fprintf(stderr, "Could not set pin %d pull up/down\n", i);
      exit(EXIT_FAILURE);
    }
      
  }

  for (i = 0; i < sizeof(all_input_pins)/sizeof(all_input_pins[0]); i++) {
    if (gpioSetMode(all_input_pins[i], PI_INPUT) != 0) {
      fprintf(stderr, "Could not set pin %d to input\n", i);
      exit(EXIT_FAILURE);
    }
    if (gpioSetPullUpDown(all_output_pins[i], PI_PUD_OFF) != 0) {
      fprintf(stderr, "Could not set pin %d pull up/down\n", i);
      exit(EXIT_FAILURE);
    }
  }
}

void printref(void)
{
#if 0
  printf("  REF a: %hu (%04hx), b: %hu (%04hx), c: %u (%08x)\n", a_ref,
	 a_ref, b_ref, b_ref, c_ref, c_ref);
#endif
}

void cycle_clk(void)
{
  usleep(DELAY);
  gpioWrite(CLK_PIN, 1);
  usleep(DELAY);
  gpioWrite(CLK_PIN, 0);
  usleep(DELAY);
}

/* These command functions require that CLK == 0 when
 * they are called.
 */

void do_reset(void)
{
  gpioWrite(CMD1_PIN, 0);
  gpioWrite(CMD0_PIN, 0);
  cycle_clk();

  a_ref = 0;
  b_ref = 0;
  c_ref = 0;
  printref();
}

void do_sum(void)
{
  gpioWrite(CMD1_PIN, 1);
  gpioWrite(CMD0_PIN, 0);
  cycle_clk();

  c_ref += a_ref * b_ref;
  printref();
}

void do_read(void)
{
  int i;
  uint32_t val = 0;

  gpioWrite(CMD1_PIN, 1);
  gpioWrite(CMD0_PIN, 1);

  /* dout is already the 1st bit so read before
   * cycling the clock.
   */
  for (i = 0; i < 2*WIDTH; i++) {
    val = (val << 1) | gpioRead(DOUT_PIN);
    cycle_clk();
  }
  
  printf("c = %u (%08x)\n", val, val);
  printref();
}

void do_write(uint16_t a, uint16_t b)
{
  int i;
  uint32_t bit, val;
  
  a_ref = a;
  b_ref = b;

  gpioWrite(CMD1_PIN, 0);
  gpioWrite(CMD0_PIN, 1);

  val = (b << WIDTH) | a;
  for (i = 0; i < 2*WIDTH; i++) {
    bit = val & 1;
    val = val >> 1;
    gpioWrite(DIN_PIN, bit);
    cycle_clk();
  }

  printref();
}

void do_help(void)
{
  printf("Commands:\n");
  printf("  reset : Sets all registers (a, b, and c) to zero\n");
  printf("  write a b : Write decimal values a and b to registers a and b\n");
  printf("  sum       : Do the c = c + a*b operation\n");
  printf("  read      : read the value of c\n");
  printf("  quit      : exit this program\n");
  printf("  help      : show this help\n\n");
}


int main (int argc, char **argv)
{
  int keep_on = 1;
  uint16_t a, b;
  char buf[80];

  printf("Type help\n");

  setup_gpios();

  // Get CLK low ASAP, commands assume CLK is already low
  if (gpioWrite(CLK_PIN, 0) != 0) {
    fprintf(stderr, "gpio write is not working\n");
    exit(EXIT_FAILURE);
  }

  /* Do initial reset */
  do_reset();

  while (keep_on) {
    if (fgets(buf, sizeof(buf), stdin) != buf) {
      rewind(stdin);
      continue;
    }
    if (strncmp("quit\n", buf, sizeof(buf)) == 0) {
      printf("Goodbye\n");
      keep_on = 0;
    } else if (strncmp("help\n", buf, sizeof(buf)) == 0) {
      do_help();
    } else if (strncmp("reset\n", buf, sizeof(buf)) == 0) {
      do_reset();
    } else if (strncmp("clear\n", buf, sizeof(buf)) == 0) {
      do_reset();
    } else if (strncmp("sum\n", buf, sizeof(buf)) == 0) {
      do_sum();
    } else if (strncmp("read\n", buf, sizeof(buf)) == 0) {
      do_read();
    } else if (sscanf(buf, "write %hu %hu\n", &a, &b) == 2) {
      do_write(a, b);
    } else if (sscanf(buf, "load %hu %hu\n", &a, &b) == 2) {
      do_write(a, b);
    } else {
      printf("Illegal command\n");
    }
  } 

  cleanup();

  return 0;
}
