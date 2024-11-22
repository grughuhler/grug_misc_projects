/* Copyright 2024 Grug Huhler.  License SPDX BSD-2-Clause.

   This is a test program showing the MCP4131 digital
   potentiometer controlled by spidev on Raspberry Pi.
*/

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

//#define DEBUG 1

#define SPIDEV "/dev/spidev0.0"
#define SPI_WR_HZ 2000000
#define SPI_RD_HZ 250000
#define CMD_WRITE 0x0
#define CMD_READ  0x3
#define CMD_INCR  0x1
#define CMD_DECR  0x2

static int spi_fd;
static int keep_going = 1;

void stop(void)
{
  keep_going = 0;
}

unsigned char one_byte_cmd(unsigned char addr, unsigned char cmd)
{
  unsigned char tx[1];
  unsigned char rx[1];
  struct spi_ioc_transfer spi;

  /* The 0x3 is needed in case of CMD_ERR */
  tx[0] = (addr << 4) | (cmd << 2) | 0x3;

  memset (&spi, 0, sizeof(spi));
  spi.tx_buf =(unsigned long) tx;
  spi.rx_buf =(unsigned long) rx;
  spi.len = 1;
  spi.delay_usecs = 0;
  spi.speed_hz = SPI_WR_HZ;
  spi.bits_per_word = 8;
  ioctl(spi_fd, SPI_IOC_MESSAGE(1), &spi);

  return rx[0];
}

unsigned short two_byte_cmd(unsigned char addr, unsigned char cmd,
                            unsigned char data)
{
  unsigned char tx[2];
  unsigned char rx[2];
  unsigned short res;
  struct spi_ioc_transfer spi;

  /* The 0x3 is needed in case of CMD_ERR */
  tx[0] = (addr << 4) | (cmd << 2) |
    ((cmd == CMD_READ) ? 0x3 : 0x2);

  /* For reads must drive 0xff, for writes drive the data */
  tx[1] = (cmd == CMD_READ) ? 0xff : data;

  memset (&spi, 0, sizeof(spi));
  spi.tx_buf =(unsigned long) tx;
  spi.rx_buf =(unsigned long) rx;
  spi.len = 2;
  spi.delay_usecs = 0;
  spi.speed_hz = (cmd == CMD_READ) ? SPI_RD_HZ : SPI_WR_HZ;
  spi.bits_per_word = 8;
  ioctl(spi_fd, SPI_IOC_MESSAGE(1), &spi);

  res = rx[1] | (rx[0] << 8);
  return res;
}

void pot_err(void)
{
  unsigned char res;
  
  res = one_byte_cmd(5, CMD_DECR);
#ifdef DEBUG
  printf("err: 0x%02x\n", res);
#endif  
}

void pot_decr(void)
{
  unsigned char res;
  
  res = one_byte_cmd(0, CMD_DECR);
#ifdef DEBUG
  printf("decr: 0x%02x\n", res);
#endif  
}

void pot_incr(void)
{
  unsigned char res;
  
  res = one_byte_cmd(0, CMD_INCR);
#ifdef DEBUG
  printf("incr: 0x%02x\n", res);
#endif  
}

void pot_read(unsigned int addr)
{
  unsigned short res;

  res = two_byte_cmd(addr, CMD_READ, 0);
  printf("read: 0x%04x\n", res);
}

void pot_write(unsigned int addr, unsigned int val)
{
  unsigned short res;

  res = two_byte_cmd(addr, CMD_WRITE, val);
#ifdef DEBUG
  printf("write: 0x%04x\n", res);
#endif  
}

void pot_tri_id(unsigned int n)
{
  unsigned int i;
  int j;

  pot_write(0, 0);
  for (i = 0; i < n; i++) {
    for (j = 0; j < 129; j++) pot_incr();
    for (j = 0; j < 128; j++) pot_decr();
  }
  printf("done\n");
}

void pot_tri_wr(unsigned int n)
{
  unsigned int i;
  int j;

  for (i = 0; i < n; i++) {
    for (j = 0; j < 129; j++) pot_write(0, j);
    for (j = 128; j >= 0; j--) pot_write(0, j);
  }
  printf("done\n");
}

void rtest(void)
{
  unsigned int v;
  unsigned int cnt = 0;
  unsigned short res;

  for (v = 0; v <= 128; v++) {
    pot_write(0, v);
    res = two_byte_cmd(0, CMD_READ, 0) & 0xff;
    if (res  != v) {
      cnt++;
      printf("%hd != %d\n", res, v);
    }
  }

  printf("err count = %d\n", cnt);
}

void help(void)
{
  printf("de: decrement wiper\n"
         "in: increment wiper\n"
         "er: erronous decrement\n"
         "rd addr: read (addr 0 is wiper)\n"
         "rt: a test of reads\n"
         "wr addr val: write\n"
         "st: stop and exit\n"
         "ti N: N triangles using incr/decr\n"
         "tw N: N triangles using write\n"
         "  All numbers in hex\n");
}
 
/* struct command lists available commands.  See function help.
 * Each function takes one or two arguments.  The table below
 * contains a pointer to the function to call for each command.
 */

struct command {
  char *cmd_string;
  int num_args;
  union {
    void (*func0)(void);
    void (*func1)(unsigned int val);
    void (*func2)(unsigned int val1, unsigned int val2);
  } u;
} commands[] = {
  {"he", 0, .u.func0=help},
  {"de", 0, .u.func0=pot_decr},
  {"er", 0, .u.func0=pot_err},
  {"in", 0, .u.func0=pot_incr},
  {"rd", 1, .u.func1=pot_read},
  {"wr", 2, .u.func2=pot_write},
  {"st", 0, .u.func0=stop},
  {"ti", 1, .u.func1=pot_tri_id},
  {"tw", 1, .u.func1=pot_tri_wr},
  {"rt", 0, .u.func0=rtest},
};

void eat_spaces(char **buf, unsigned int *len)
{
  while (*len > 0) {
    if (**buf == ' ') {
      *buf += 1;
      *len -= 1;
    } else
      break;
  }
}

/* Returns 1 if a number found, else 0.  Number is in *v */

unsigned int get_hex(char **buf, unsigned int *len, unsigned int *v)
{
  int valid = 0;
  int keep_going;
  char ch;

  keep_going = 1;

  *v = 0;
  while (keep_going && (*len > 0)) {

    ch = **buf;
    *buf += 1;
    *len -= 1;

    if ((ch >= '0') && (ch <= '9')) {
      *v = 16*(*v) + (ch - '0');
      valid = 1;
    } else if ((ch >= 'a') && (ch <= 'f')) {
      *v = 16*(*v) + (ch - 'a' + 10);
      valid = 1;
    } else if ((ch >= 'A') && (ch <= 'F')) {
      *v = 16*(*v) + (ch - 'A' + 10);
      valid = 1;
    } else {
      keep_going = 0;
    }
  }

  return valid;
}

void parse(char *buf, unsigned int len)
{
  int i, cmd_not_ok;
  unsigned int val1, val2;

  cmd_not_ok = 1;
  eat_spaces(&buf, &len);
  if (len < 2) goto err;

  for (i = 0; i < sizeof(commands)/sizeof(commands[0]); i++)
    if ((buf[0] == commands[i].cmd_string[0]) && (buf[1] == commands[i].cmd_string[1])) {
      buf += 2;
      len -= 2;
      switch (commands[i].num_args) {
      case 0:
        commands[i].u.func0();
        cmd_not_ok = 0;
        break;
      case 1:
        eat_spaces(&buf, &len);
        if (get_hex(&buf, &len, &val1)) {
          commands[i].u.func1(val1);
          cmd_not_ok = 0;
        }
        break;
      case 2:
        eat_spaces(&buf, &len);
        if (get_hex(&buf, &len, &val1)) {
          eat_spaces(&buf, &len);
          if (get_hex(&buf, &len, &val2)) {
            commands[i].u.func2(val1, val2);
            cmd_not_ok = 0;
          }
        }
        break;
      default:
        break;
      }
      break;
    }

 err:
  if (cmd_not_ok)
    printf("illegal command, he for help\n");
}

#define BUFLEN 64


int main()
{
  char buf[BUFLEN];
  int len;
  
  if ((spi_fd = open(SPIDEV, O_RDWR)) < 0) {
    return EXIT_FAILURE;
  }

  while (keep_going) {
    fgets(buf, sizeof(buf), stdin);
    len = strlen(buf);
    if (len > 0) parse(buf, len);
  }

  close(spi_fd);
  return 0;
}
