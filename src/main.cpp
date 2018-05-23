#include <Arduino.h>
void yield(void) { }

#include <util/delay.h>
#include <avr/interrupt.h>

// using register D
#define OUT_0(bit) PORTD &= ~_BV(bit);
#define OUT_1(bit) PORTD |=  _BV(bit);

#define MODE_OUT(bit) DDRD |= _BV(bit);
#define MODE_IN(bit) DDRD &= ~_BV(bit); OUT_0(bit)
#define MODE_IN_UP(bit) DDRD &= ~_BV(bit); OUT_1(bit)
#define READ(bit) (PIND & _BV(bit))

#define SERDBG

#define MELBUS_CLOCKBIT_INT 1 // interrupt numer (INT1) on DDR3
#define MELBUS_CLOCKBIT 3 // Pin D3 - CLK
#define MELBUS_DATA 4 // Pin D4 - Data
#define MELBUS_BUSY 5 // Pin D5 - Busy

volatile uint8_t melbus_ReceivedByte = 0;
volatile uint8_t melbus_CharBytes = 0;
volatile uint8_t melbus_OutByte = 0xFF;
volatile uint8_t melbus_SendBuffer[9] = {0x00,0x02,0x00,0x01,0x80,0x01,0xC7,0x0A,0x02};
volatile uint8_t melbus_SendCnt = 0;
volatile uint8_t melbus_DiscBuffer[6] = {0x00,0xFC,0xFF,0x4A,0xFC,0xFF};
volatile uint8_t melbus_DiscCnt = 0;
volatile uint8_t melbus_Bitposition = 0x80;
volatile uint8_t _M[12] = {0,0,0,0,0,0,0,0,0,0,0,0};

#define GET_MACRO(_1,_2,_3,NAME,...) NAME
#define M(i,...) GET_MACRO(__VA_ARGS__, M_3, M_2, M_1)(i,__VA_ARGS__)
#define M_1(i,x1) (_M[i] == x1)
#define M_2(i,x1,x2) (M_1(i,x1) || M_1(i,x2))
#define M_3(i,x1,x2,x3) (M_1(i,x1) || M_1(i,x2) || M_1(i,x3))

volatile bool InitialSequence_ext = false;
volatile bool ByteIsRead = false;
volatile bool sending_byte = false;
volatile bool melbus_MasterRequested = false;
volatile bool melbus_MasterRequestAccepted = false;

volatile int incomingByte = 0; // for incoming serial data

// Global external interrupt that triggers when clock pin goes high after it
// has been low for a short time => time to read datapin
void MELBUS_CLOCK_INTERRUPT() {
  // Read status of Datapin and set status of current bit in recv_byte
  if (melbus_OutByte & melbus_Bitposition) {
    MODE_IN_UP(MELBUS_DATA)
  } else { // if bit [i] is "0" - make databpin low
    OUT_0(MELBUS_DATA)
    MODE_OUT(MELBUS_DATA)
  }

  if (READ(MELBUS_DATA)) {
    // set bit nr [melbus_Bitposition] to "1"
    melbus_ReceivedByte |= melbus_Bitposition;
  } else {
    // set bit nr [melbus_Bitposition] to "0"
    melbus_ReceivedByte &= ~melbus_Bitposition;
  }

  // if all the bits in the byte are read:
  if (melbus_Bitposition==0x01) {

    // Move every lastreadbyte one step down the array to keep track of former
    // bytes
    for (int i=11; i>0; --i) {
      _M[i] = _M[i-1];
    }

    if (melbus_OutByte != 0xFF) {
      _M[0] = melbus_OutByte;
      melbus_OutByte = 0xFF;
    } else {
      // Insert the newly read byte into first position of array
      _M[0] = melbus_ReceivedByte;
    }
    // set bool to true to evaluate the bytes in main loop
    ByteIsRead = true;

    // Reset bitcount to first bit in byte
    melbus_Bitposition = 0x80;
    if (M(2,0x07) && M(1,0x1A,0x4A) && M(0,0xEE)) {
      InitialSequence_ext = true;
    } else if (M(2,0x0) && M(1,0x1C,0x4C) && M(0,0xED)) {
      InitialSequence_ext = true;
    } else if (M(0,0xE8,0xE9) && InitialSequence_ext) {
      InitialSequence_ext = false;

      // Returning the expected byte to the HU, to confirm that the CD-CHGR is
      // present (0xEE)! see "ID Response"-table here
      // http://volvo.wot.lv/wiki/doku.php?id=melbus
      melbus_OutByte = 0xEE;
    } else if (M(2,0xE8,0xE9) && M(1,0x1E,0x4E) && M(0,0xEF)) {
      // CartInfo
      melbus_DiscCnt=6;
    } else if (M(2,0xE8,0xE9) && M(1,0x19,0x49) && M(0,0x22)) {
      // Powerdown
      melbus_OutByte = 0x00; // respond to powerdown;
      melbus_SendBuffer[1] = 0x02; // STOP
      melbus_SendBuffer[8] = 0x02; // STOP
    } else if (M(2,0xE8,0xE9) && M(1,0x19,0x49) && M(0,0x52)) {
      // RND
    } else if (M(2,0xE8,0xE9) && M(1,0x19,0x49) && M(0,0x29)) {
      // FF
    } else if (M(2,0xE8,0xE9) && M(1,0x19,0x49) && M(0,0x2F)) {
      // FR
      melbus_OutByte = 0x00; // respond to start;
      melbus_SendBuffer[1] = 0x08; // START
      melbus_SendBuffer[8] = 0x08; // START
    } else if (M(3,0xE8,0xE9) && M(2,0x1A,0x4A) && M(1,0x50) && M(0,0x01)) {
      // D-
      --melbus_SendBuffer[3];
      melbus_SendBuffer[5] = 0x01;
    } else if (M(3,0xE8,0xE9) && M(2,0x1A,0x4A) && M(1,0x50) && M(0,0x41)) {
      // D+
      ++melbus_SendBuffer[3];
      melbus_SendBuffer[5] = 0x01;
    } else if (M(4,0xE8,0xE9) && M(3,0x1B,0x4B) && M(2,0x2D) && M(1,0x00) && M(0,0x01)) {
      // T-
      --melbus_SendBuffer[5];
    } else if (M(4,0xE8,0xE9) && M(3,0x1B,0x4B) && M(2,0x2D) && M(1,0x40) && M(0,0x01)) {
      // T+
      ++melbus_SendBuffer[5];
    } else if (M(4,0xE8,0xE9) && M(3,0x1B,0x4B) && M(2,0xE0) && M(1,0x01) && M(0,0x08)) {
      // Playinfo
      melbus_SendCnt = 9;
    }

    if (melbus_SendCnt) {
      melbus_OutByte = melbus_SendBuffer[9-melbus_SendCnt];
      --melbus_SendCnt;
    } else if (melbus_DiscCnt) {
      melbus_OutByte = melbus_DiscBuffer[6-melbus_DiscCnt];
      --melbus_DiscCnt;
    }
  } else {
    // set bitnumber to address of next bit in byte
    melbus_Bitposition >>= 1;
  }
  EIFR |= (1 << INTF1);
}

// Notify HU that we want to trigger the first initiate procedure to add a new
// device (CD-CHGR) by pulling BUSY line low for 1s
void melbus_Init_CDCHRG() {
  // Disabel interrupt on INT1 quicker then:
  // detachInterrupt(MELBUS_CLOCKBIT_INT);
  EIMSK &= ~(1<<INT1);

  // Wait until Busy-line goes high (not busy) before we pull BUSY low to
  // request init
  while (!READ(MELBUS_BUSY)) { }
  _delay_us(10);

  MODE_OUT(MELBUS_BUSY);
  OUT_0(MELBUS_BUSY);
  _delay_ms(1200);
  OUT_1(MELBUS_BUSY);
  MODE_IN_UP(MELBUS_BUSY);

  // Enable interrupt on INT1, quicker then:
  // attachInterrupt(MELBUS_CLOCKBIT_INT, MELBUS_CLOCK_INTERRUPT, RISING);
  EIMSK |= (1<<INT1);
}

void loop() { // LOOP ===============================================
  // Waiting for the clock interrupt to trigger 8 times to read one byte before
  // evaluating the data
#ifdef SERDBG
  if (ByteIsRead) {
    // Reset bool to enable reading of next byte
    ByteIsRead = false;

    if (incomingByte == ' ') {
      if ( M(11,0x0)
        && M(10,0x4A,0x4C,0x4E)
        && M(9,0xEC)
        && M(8,0x57)
        && M(7,0x57)
        && M(6,0x49)
        && M(5,0x52)
        && M(4,0xAF)
        && M(3,0xE0)
        && M(2,0x0)
      ) {
        melbus_CharBytes = 8; // print RDS station name
      }

      if (M(1,0x0) && M(0,0x4A))
        Serial.println("\n LCD is master: (no CD init)");
      else if (M(1,0x0) && M(0,0x4C))
        Serial.println("\n LCD is master: (\?\?\?)");
      else if (M(1,0x0) && M(0,0x4E))
        Serial.println("\n LCD is master: (with CD init)");
      else if (M(1,0x80) && M(0,0x4E))
        Serial.println("\n ???");
      else if (M(1,0xE8) && M(0,0x4E))
        Serial.println("\n ???");
      else if (M(1,0xF9) && M(0,0x49))
        Serial.println("\n HU  is master: ");
      else if (M(1,0x80) && M(0,0x49))
        Serial.println("\n HU  is master: ");
      else if (M(1,0xE8) && M(0,0x49))
        Serial.println("\n HU  is master: ");
      else if (M(1,0xE9) && M(0,0x4B))
        Serial.println("\n HU  is master: -> CDC");
      else if (M(1,0x81) && M(0,0x4B))
        Serial.println("\n HU  is master: -> CDP");
      else if (M(1,0xF9) && M(0,0x4E))
        Serial.println("\n HU  is master: ");
      else if (M(1,0x50) && M(0,0x4E))
        Serial.println("\n HU  is master: ");
      else if (M(1,0x50) && M(0,0x4C))
        Serial.println("\n HU  is master: ");
      else if (M(1,0x50) && M(0,0x4A))
        Serial.println("\n HU  is master: ");
      else if (M(1,0xF8) && M(0,0x4C))
        Serial.println("\n HU  is master: ");

      if (melbus_CharBytes) {
        Serial.write(_M[1]);
        --melbus_CharBytes;
      } else {
        Serial.print(_M[1],HEX);
        Serial.write(' ');
      }
    }
  }
#endif

  // If BUSYPIN is HIGH => HU is in between transmissions
  if (READ(MELBUS_BUSY)) {
    // Make sure we are in sync when reading the bits by resetting the clock
    // reader

#ifdef SERDBG
    if (melbus_Bitposition != 0x80) {
      Serial.println(melbus_Bitposition,HEX);
      Serial.println("\n not in sync! ");
    }
#endif
    if (incomingByte != 'k') {
      melbus_Bitposition = 0x80;
      melbus_OutByte = 0xFF;
      melbus_SendCnt = 0;
      melbus_DiscCnt = 0;
      MODE_IN_UP(MELBUS_DATA)
    }
  }
#ifdef SERDBG
  if (Serial.available() > 0) { // read the incoming byte
    incomingByte = Serial.read();
  }
  if (incomingByte == 'i') {
    melbus_Init_CDCHRG();
    Serial.println("\n forced init: ");
    incomingByte=0;
  }
#endif
  if ((melbus_Bitposition == 0x80) && READ(MELBUS_CLOCKBIT)) {
    _delay_us(7);
    MODE_IN_UP(MELBUS_DATA)
  }
}

int main(void) {
  // INIT ===========================================================
  MODE_IN_UP(MELBUS_DATA); // Data is deafult input high

  // Activate interrupt on clock pin (INT1, D3)
  attachInterrupt(MELBUS_CLOCKBIT_INT, MELBUS_CLOCK_INTERRUPT, FALLING);
  // Set Clockpin-interrupt to input
  MODE_IN_UP(MELBUS_CLOCKBIT);

#ifdef SERDBG
  // Initiate serial communication to debug via serial-usb (arduino)
  Serial.begin(230400);
  Serial.println("Initiating contact with Melbus:");
#endif
  // Call function that tells HU that we want to register a new device
  melbus_Init_CDCHRG();
  // ================================================================

  for (;;) loop();

  return 0;
}
