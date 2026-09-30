#include "aip33628.h"
#include <string.h>

#include <driver/spi_master.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

// COM scan phase pattern for COM0+COM1, COM2+COM3, COM4+COM5,
// COM6+COM7
static const uint8_t scan_pattern[4] = {0x03, 0x0C, 0x30, 0xC0};

// Driver GPIO maps
static uint8_t clk_pins[NUM_DRIVERS];
static uint8_t data_pins[NUM_DRIVERS];

// State representation: uint16_t segments[NUM_DRIVERS][4]
static uint16_t segments[NUM_DRIVERS][4];
static volatile uint8_t current_is_val = 0; // Default IS current setting = 0

static esp_timer_handle_t frame_timer = NULL;
static spi_device_handle_t spi_handle = NULL;

/**
 * @brief Initialize ESP32 Hardware SPI2 for 1 MHz LSB-first
 * transmission.
 */

static void init_spi(void) {
  if (spi_handle != NULL) return;

  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = data_pins[1];
  buscfg.miso_io_num = -1;
  buscfg.sclk_io_num = clk_pins[1];
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 32;

  spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = 1000000; // 1 MHz SPI clock rate (datasheet max)
  devcfg.mode = 0;                 // SPI Mode 0 (CPOL=0, CPHA=0)
  devcfg.spics_io_num = -1;        // CS not used
  devcfg.queue_size = 1;
  devcfg.flags = SPI_DEVICE_BIT_LSBFIRST; // LSB first transmission

  spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
}


/**
 * @brief Transmit single 30-bit frame to an AiP33628 driver.  Uses
 * Hardware SPI for bits 0..28 @ 1 MHz, and 30th bit + Latch sequence
 * in GPIO.
 */

static void send_frame_driver(uint8_t driver_idx, uint8_t phase_idx)
{
  uint8_t clk_pin = clk_pins[driver_idx];
  uint8_t data_pin = data_pins[driver_idx];

  uint16_t seg_val = segments[driver_idx][phase_idx];
  uint8_t com_val = scan_pattern[phase_idx];
  uint8_t is_val = current_is_val & 0x0F;

  // Pack 30 bits into uint32_t:
  // Bits 0..15  : seg_val
  // Bits 16..23 : com_val
  // Bits 24..27 : is_val
  // Bits 28..29 : 0
  uint32_t packet = ((uint32_t)seg_val) |
    (((uint32_t)com_val) << 16) |
    (((uint32_t)is_val) << 24);

  // Ensure pins are standard GPIO outputs for Start condition
  esp_rom_gpio_connect_out_signal(clk_pin, SIG_GPIO_OUT_IDX,
                                  false, false);
  esp_rom_gpio_connect_out_signal(data_pin, SIG_GPIO_OUT_IDX,
                                  false, false);
  pinMode(clk_pin, OUTPUT);
  pinMode(data_pin, OUTPUT);

  // Start Control Signal: CLK is LOW, DATA transitions LOW -> HIGH
  digitalWrite(clk_pin, LOW);
  digitalWrite(data_pin, LOW);
  delayMicroseconds(1);
  digitalWrite(data_pin, HIGH);
  delayMicroseconds(1);

  // Connect target driver pins to Hardware SPI peripheral matrix
  esp_rom_gpio_connect_out_signal(clk_pin, FSPICLK_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(data_pin, FSPID_OUT_IDX, false, false);

  // Hardware SPI transfer of bits 0..28 (29 bits) at 1 MHz (LSB
  // first)
  spi_transaction_t t = {};
  t.length = 29; // 29 bits shifted by SPI hardware
  t.tx_buffer = &packet;
  spi_device_polling_transmit(spi_handle, &t);

  // Reconnect pins back to GPIO output mode for 30th bit & Latch
  // sequence
  esp_rom_gpio_connect_out_signal(clk_pin, SIG_GPIO_OUT_IDX,
                                  false, false);
  esp_rom_gpio_connect_out_signal(data_pin, SIG_GPIO_OUT_IDX,
                                  false, false);

  // 30th Bit (Bit 29 = 0) & Latch Sequence (DATA LOW -> HIGH while
  // 30th CLK is HIGH)
  digitalWrite(data_pin, LOW);
  delayMicroseconds(1);
  digitalWrite(clk_pin, HIGH); // 30th CLK pulse rising edge
  delayMicroseconds(1);

  // Latch trigger DURING 30th CLK pulse: DATA LOW -> HIGH while CLK
  // is HIGH
  digitalWrite(data_pin, HIGH);
  delayMicroseconds(1);

  digitalWrite(clk_pin, LOW);  // 30th CLK pulse falling edge
  delayMicroseconds(1);
  digitalWrite(data_pin, LOW);
}

static void send_frame_cb(void* arg)
{
  static uint8_t com_phase = 0;

  for (uint8_t d = 0; d < NUM_DRIVERS; d++) {
    send_frame_driver(d, com_phase);
  }

  com_phase = (com_phase + 1) % 4;
}

void aip33628_init(uint8_t driver1_clock_pin, uint8_t driver1_data_pin,
                   uint8_t driver2_clock_pin, uint8_t driver2_data_pin)
{
  clk_pins[0] = driver1_clock_pin;
  clk_pins[1] = driver2_clock_pin;
  data_pins[0] = driver1_data_pin;
  data_pins[1] = driver2_data_pin;

  // Configure GPIO pins as outputs and set initial LOW state
  for (uint8_t d = 0; d < NUM_DRIVERS; d++) {
    pinMode(clk_pins[d], OUTPUT);
    pinMode(data_pins[d], OUTPUT);
    digitalWrite(clk_pins[d], LOW);
    digitalWrite(data_pins[d], LOW);
  }

  // Datasheet Section 4.2 (Power-On Reset): Hold lines idle for at
  // least 200us after power-on while internal registers stabilize.
  delayMicroseconds(250);

  init_spi();

  // Clear segments state in memory
  aip33628_clear_all();

  // Immediately send a full 4-phase blank frame sequence to all
  // driver hardware instances synchronously.
  for (uint8_t phase = 0; phase < 4; phase++) {
    for (uint8_t d = 0; d < NUM_DRIVERS; d++) {
      send_frame_driver(d, phase);
    }
  }

  // Setup microsecond esp_timer for 1.25ms (1250us) periodic
  // execution.  4 scan phases * 1.25ms = 5ms total cycle period ->
  // 200 Hz LED refresh rate.
  if (frame_timer == NULL) {
    const esp_timer_create_args_t timer_args = {
      .callback = &send_frame_cb,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "aip33628_timer",
      .skip_unhandled_events = false
    };
    esp_timer_create(&timer_args, &frame_timer);
    // 1250 microseconds = 1.25ms (200 Hz LED refresh)
    esp_timer_start_periodic(frame_timer, 1250);
  }
}

void aip33628_set_current(uint8_t is_val)
{
  current_is_val = is_val & 0x0F;
}

void aip33628_set_seg(uint8_t driver_index, uint8_t phase_index,
                      uint16_t seg_value)
{
  if (driver_index < NUM_DRIVERS && phase_index < 4) {
    segments[driver_index][phase_index] = seg_value;
  }
}

void aip33628_ena_led(uint8_t driver_index, uint8_t phase_index,
                      uint8_t seg_bit)
{
  if (driver_index < NUM_DRIVERS && phase_index < 4 && seg_bit < 16) {
    segments[driver_index][phase_index] |= (1U << seg_bit);
  }
}

void aip33628_dis_led(uint8_t driver_index, uint8_t phase_index,
                      uint8_t seg_bit)
{
  if (driver_index < NUM_DRIVERS && phase_index < 4 && seg_bit < 16) {
    segments[driver_index][phase_index] &= ~(1U << seg_bit);
  }
}

void aip33628_clear_all(void)
{
  memset(segments, 0, sizeof(segments));
}
