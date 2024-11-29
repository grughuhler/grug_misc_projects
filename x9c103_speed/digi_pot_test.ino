/* License SPDX BSD-2-Clause
 *  
 *  This is a test program for the Renesas X9C10x 100-position digital
 *  potentiometer.
 *  
 *  It reads from the Arduino serial monitor.  It starts at wiper
 *  position 0 and prompts you for a new value.
 */

/* wiper steps between min and max */
#define POT_NUM_POS 100

/* Change these two for the timing experiment.  Minimum value for each
 *  is 1.  Max possibly needed is 50 for each.  Values well above 1
 *  up less than 50 might work pretty well, but no guarantees from the
 *  datasheet.
 */

#define PIN_CS  16
#define PIN_INC 4
#define PIN_UD  17

int pos;
unsigned int delay_usec = 50;

void wiper_change(boolean up, boolean store)
{
  unsigned int ud_val;
  
  if (up) ud_val = HIGH; else ud_val = LOW;
  
  digitalWrite(PIN_UD, ud_val);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(delay_usec);
  digitalWrite(PIN_INC, LOW);
  delayMicroseconds(delay_usec);
  if (store) {
    digitalWrite(PIN_INC, HIGH);
    delayMicroseconds(delay_usec);
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_UD, HIGH);
  } else {
    digitalWrite(PIN_CS, HIGH);
    digitalWrite(PIN_UD, HIGH);
    delayMicroseconds(delay_usec);
    digitalWrite(PIN_INC, HIGH);   
  }
  delayMicroseconds(delay_usec);
}


void set_to_zero(void)
{
  int i;

  for (i = 0; i < POT_NUM_POS - 1; i++) wiper_change(false, false);
  pos = 0;
}

void set_to_pos2(int new_pos)
{
  int i, diff, up;

  if (new_pos > (POT_NUM_POS - 1)) new_pos = POT_NUM_POS - 1;
  if (new_pos < 0) new_pos = 0;

  diff = new_pos - pos;
  if (diff == 0) {
     return;
  } else if (diff < 0) {
    up = 0;
    diff = -diff;
  } else {
    up = 1;
  }

  digitalWrite(PIN_UD, up);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(1);
  
  for (i = 0; i < diff; i++) {
    digitalWrite(PIN_INC, LOW);
    delayMicroseconds(delay_usec);
    // Delay INC# going high for last one to avoid store
    if (i != (diff - 1)) digitalWrite(PIN_INC, HIGH);
    delayMicroseconds(delay_usec);
  }
  // CS# goes high before INC# to avoid store
  digitalWrite(PIN_CS, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_INC, HIGH);
  delayMicroseconds(1);
  
  pos = new_pos; 
}



void sweep(void)
{
  int i;

  Serial.println("Triangle");
  set_to_zero();
  for (i = 0; i < POT_NUM_POS - 1; i++) wiper_change(true, false);
  for (i = 0; i < POT_NUM_POS - 1; i++) wiper_change(false, false);
  set_to_zero();
}

void slow_ramp(void)
{
  int i;

  Serial.println("Slow ramp");
  set_to_zero();
  delay(1500);

  for (i = 0; i < POT_NUM_POS - 1; i++) {
    wiper_change(true, false);
    delay(100);
  }

  delay(1500);
  set_to_zero();
}

// the setup function runs once when you press reset or power the board
void setup()
{
  int i;

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);

  pinMode(PIN_INC, OUTPUT);
  digitalWrite(PIN_INC, HIGH);

  pinMode(PIN_UD, OUTPUT);
  digitalWrite(PIN_UD, HIGH);

  Serial.begin(115200);
  delay(500);

  Serial.println("Welcome to digi pot, start scope");

  set_to_zero();
}

// the loop function runs over and over again forever
void loop()
{
  int new_pos = 0;
  boolean store = false;
  String buf;
  unsigned int tmp;

  Serial.print("Enter 0 to ");
  Serial.print(POT_NUM_POS - 1);
  Serial.println(" to set pot or tUSEC to set delay (like t5) for 2*5 usec");
  Serial.println("or enter 9999 for a triangle");
  
  while (Serial.available() == 0) {}
  buf = Serial.readString();
  buf.trim();

  if (buf[0] == 't') {
    buf = buf.substring(1);
    tmp = buf.toInt();
    if (tmp >= 0) {
      delay_usec = tmp;
      Serial.print("delay now ");
      Serial.print(delay_usec);
      Serial.println(" usec for both low and high");
    } else {
      Serial.println("delay unchanged, value out of bounds");
    }
    return;
  } else {
    new_pos = buf.toInt();
  }

  if (new_pos == 9999)
    sweep();
  else if (new_pos == 9998)
    slow_ramp();
  else {
    Serial.print("Setting pos ");
    Serial.print(new_pos);
    Serial.print(" with delay ");
    Serial.print(delay_usec);
    Serial.println(" usec");
    set_to_pos2(new_pos);
  }
}
