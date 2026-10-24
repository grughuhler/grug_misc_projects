#include "aip33628.h"
#include <string.h>

#include <soc/gpio_struct.h>

// COM scan phase pattern for COM0+COM1, COM2+COM3, COM4+COM5,
// COM6+COM7.  Keep in mind that COMn corresponds to pin COMn.
//
// Notice the values in phase_pattern are bit-reversed to match
// the AIP33628 frame bit ordering.  The first phase is COM0+COM1.
// that means 1100_0000 must go on the wire.
static const uint8_t phase_pattern[4] = {0xc0, 0x30, 0x0c, 0x03};

// Driver GPIO maps
static uint8_t clk_pins[NUM_DRIVERS];
static uint8_t data_pins[NUM_DRIVERS];

// State representation: uint16_t segments[NUM_DRIVERS][4]
static uint16_t segments[NUM_DRIVERS][4];

// Current setting.  From dimest to brightest 0, 1, 2, ..., 15
// Note that this must be bit reversed on the wire.
static volatile uint8_t current_is_val = 0;

static esp_timer_handle_t frame_timer = NULL;

// ~100 ns delay at 160 MHz
static inline void delay_100ns(void)
{
  for (int i = 0; i < 4; i++) {
    __asm__ __volatile__("nop");
  }
}

static uint8_t reverse_4bit_bitwise(uint8_t n) {
    // 1. Swap adjacent bits: (b3 b2 b1 b0) -> (b2 b3 b0 b1)
    n = ((n & 0x05) << 1) | ((n & 0x0A) >> 1);
    // 2. Swap 2-bit pairs:   (b2 b3 b0 b1) -> (b0 b1 b2 b3)
    n = ((n & 0x03) << 2) | ((n & 0x0C) >> 2);
    return n;
}

/**
 * @brief Transmit single 30-bit frame to an AiP33628 driver using
 * direct atomic GPIO set/clear register writes (GPIO.out_w1ts / out_w1tc).
 */
static void send_frame_driver(uint8_t driver_idx, uint8_t phase_idx)
{
  uint32_t clk_mask  = (1UL << clk_pins[driver_idx]);
  uint32_t data_mask = (1UL << data_pins[driver_idx]);

  uint16_t seg_val = segments[driver_idx][phase_idx];
  uint8_t com_val  = phase_pattern[phase_idx];
  uint8_t frame_is_val;

  // Value must be bit reversed on the wire
  frame_is_val = reverse_4bit_bitwise(current_is_val & 0xf);

  // Pack 30 bits into uint32_t:
  // Bits 0..15  : seg_val
  // Bits 16..23 : com_val
  // Bits 24..27 : is_val
  // Bits 28..29 : 0 (reserved)
  uint32_t packet = ((uint32_t)seg_val) |
                    (((uint32_t)com_val) << 16) |
                    (((uint32_t)frame_is_val) << 24);

  // Entry condition: CLK and DATA are LOW.
  // Send "End Control" pulse (rising edge on DATA while CLK is LOW)
  // to exit latch mode and prepare shift register for the 30-bit frame.
  GPIO.out_w1ts.val = data_mask;
  delay_100ns();
  GPIO.out_w1tc.val = data_mask;
  delay_100ns();

  // Shift out all 30 bits (LSB first).
  // DATA only changes while CLK is solidly LOW.
  for (int bit = 0; bit < 30; bit++) {
    if (packet & 1) {
      GPIO.out_w1ts.val = data_mask;
    } else {
      GPIO.out_w1tc.val = data_mask;
    }
    packet >>= 1;
    delay_100ns();                // DATA setup time before CLK rises

    GPIO.out_w1ts.val = clk_mask; // Rising edge shifts bit into AiP33628
    delay_100ns();                // CLK high pulse width

    // Leave CLK HIGH after the 30th bit (bit == 29) for the Latch sequence
    if (bit < 29) {
      GPIO.out_w1tc.val = clk_mask;
      delay_100ns();              // Ensure CLK is LOW before DATA changes next bit
    }
  }

  // CLK is HIGH. Bring DATA LOW first (bit 29 is 0, so it is already LOW,
  // but be explicit), then create a rising edge on DATA while CLK is HIGH
  // to latch the 30-bit shift register into the output latches.
  GPIO.out_w1tc.val = data_mask;
  delay_100ns();
  GPIO.out_w1ts.val = data_mask;  // Rising edge on DATA while CLK is HIGH -> Latch!
  delay_100ns();
  GPIO.out_w1tc.val = data_mask;
  delay_100ns();
  GPIO.out_w1tc.val = clk_mask;
  delay_100ns();

  // Leave with both CLK and DATA LOW.
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
    segments[driver_index][phase_index] |= (1U << (15 - seg_bit));
  }
}

void aip33628_dis_led(uint8_t driver_index, uint8_t phase_index,
                      uint8_t seg_bit)
{
  if (driver_index < NUM_DRIVERS && phase_index < 4 && seg_bit < 16) {
    segments[driver_index][phase_index] &= ~(1U << (15 - seg_bit));
  }
}

void aip33628_clear_all(void)
{
  memset(segments, 0, sizeof(segments));
}
