#include <Arduino.h>
void yield(void) { }

#define SERDBG

const uint8_t MELBUS_CLOCKBIT_INT = 1; // interrupt numer (INT1) on DDR3
const uint8_t MELBUS_CLOCKBIT = 3; // Pin D3 - CLK
const uint8_t MELBUS_DATA = 4; // Pin D4  - Data
const uint8_t MELBUS_BUSY = 5; // Pin D5  - Busy

volatile uint8_t melbus_ReceivedByte = 0;
volatile uint8_t melbus_CharBytes = 0;
volatile uint8_t melbus_OutByte = 0xFF;
volatile uint8_t melbus_SendBuffer[9] = {0x00,0x02,0x00,0x01,0x80,0x01,0xC7,0x0A,0x02};
volatile uint8_t melbus_SendCnt = 0;
volatile uint8_t melbus_DiscBuffer[6] = {0x00,0xFC,0xFF,0x4A,0xFC,0xFF};
volatile uint8_t melbus_DiscCnt = 0;
volatile uint8_t melbus_Bitposition = 0x80;
volatile uint8_t _M[12] = {0,0,0,0,0,0,0,0,0,0,0,0};

#define M(i,x) (_M[i] == x)

volatile bool InitialSequence_ext = false;
volatile bool ByteIsRead = false;
volatile bool sending_byte = false;
volatile bool melbus_MasterRequested = false;
volatile bool melbus_MasterRequestAccepted = false;

volatile bool testbool = false;
volatile bool AllowInterruptRead = true;
volatile int incomingByte = 0; // for incoming serial data

// Global external interrupt that triggers when clock pin goes high after it
// has been low for a short time => time to read datapin
void MELBUS_CLOCK_INTERRUPT() {

  // Read status of Datapin and set status of current bit in recv_byte
  if (melbus_OutByte & melbus_Bitposition) {
    DDRD &= (~(1<<MELBUS_DATA));
    PORTD |= (1<<MELBUS_DATA);
  } else { // if bit [i] is "0" - make databpin low
    PORTD &= (~(1<<MELBUS_DATA));
    DDRD |= (1<<MELBUS_DATA);
  }

  if (PIND & (1<<MELBUS_DATA)) {
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
    if (M(2,0x07) && (M(1,0x1A) || M(1,0x4A)) && M(0,0xEE)) {
      InitialSequence_ext = true;
    } else if (M(2,0x0) && (M(1,0x1C) || M(1,0x4C)) && M(0,0xED)) {
      InitialSequence_ext = true;
    } else if ((M(0,0xE8) || M(0,0xE9)) && InitialSequence_ext) {
      InitialSequence_ext = false;

      // Returning the expected byte to the HU, to confirm that the CD-CHGR is
      // present (0xEE)! see "ID Response"-table here
      // http://volvo.wot.lv/wiki/doku.php?id=melbus
      melbus_OutByte = 0xEE;
    } else if ((M(2,0xE8) || M(2,0xE9)) && (M(1,0x1E) || M(1,0x4E)) && M(0,0xEF)) {
      // CartInfo
      melbus_DiscCnt=6;
    } else if ((M(2,0xE8) || M(2,0xE9)) && (M(1,0x19) || M(1,0x49)) && M(0,0x22)) {
      // Powerdown
      melbus_OutByte = 0x00; // respond to powerdown;
      melbus_SendBuffer[1]=0x02; // STOP
      melbus_SendBuffer[8]=0x02; // STOP
    } else if ((M(2,0xE8) || M(2,0xE9)) && (M(1,0x19) || M(1,0x49)) && M(0,0x52)) {
      // RND
    } else if ((M(2,0xE8) || M(2,0xE9)) && (M(1,0x19) || M(1,0x49)) && M(0,0x29)) {
      // FF
    } else if ((M(2,0xE8) || M(2,0xE9)) && (M(1,0x19) || M(1,0x49)) && M(0,0x2F)) {
      // FR
      melbus_OutByte = 0x00; // respond to start;
      melbus_SendBuffer[1]=0x08; // START
      melbus_SendBuffer[8]=0x08; // START
    } else if ((M(3,0xE8) || M(3,0xE9)) && (M(2,0x1A) || M(2,0x4A)) && M(1,0x50) && M(0,0x01)) {
      // D-
      --melbus_SendBuffer[3];
      melbus_SendBuffer[5] = 0x01;
    } else if ((M(3,0xE8) || M(3,0xE9)) && (M(2,0x1A) || M(2,0x4A)) && M(1,0x50) && M(0,0x41)) {
      // D+
      ++melbus_SendBuffer[3];
      melbus_SendBuffer[5]=0x01;
    } else if ((M(4,0xE8) || M(4,0xE9)) && (M(3,0x1B) || M(3,0x4B)) && M(2,0x2D) && M(1,0x00) && M(0,0x01)) {
      // T-
      --melbus_SendBuffer[5];
    } else if ((M(4,0xE8) || M(4,0xE9)) && (M(3,0x1B) || M(3,0x4B)) && M(2,0x2D) && M(1,0x40) && M(0,0x01)) {
      // T+
      ++melbus_SendBuffer[5];
    } else if ((M(4,0xE8) || M(4,0xE9)) && (M(3,0x1B) || M(3,0x4B)) && M(2,0xE0)  && M(1,0x01) && M(0,0x08) ) {
      // Playinfo
      melbus_SendCnt=9;
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

  // Wait untill Busy-line goes high (not busy) before we pull BUSY low to
  // request init
  while (digitalRead(MELBUS_BUSY)==LOW) { }
  delayMicroseconds(10);

  pinMode(MELBUS_BUSY, OUTPUT);
  digitalWrite(MELBUS_BUSY, LOW);
  delay(1200);
  digitalWrite(MELBUS_BUSY, HIGH);
  pinMode(MELBUS_BUSY, INPUT_PULLUP);

  // Enable interrupt on INT1, quicker then:
  // attachInterrupt(MELBUS_CLOCKBIT_INT, MELBUS_CLOCK_INTERRUPT, RISING);
  EIMSK |= (1<<INT1);
}

// Startup seequence
void setup() {
  // Data is deafult input high
  pinMode(MELBUS_DATA, INPUT_PULLUP);

  // Activate interrupt on clock pin (INT1, D3)
  attachInterrupt(MELBUS_CLOCKBIT_INT, MELBUS_CLOCK_INTERRUPT, FALLING);
  // Set Clockpin-interrupt to input
  pinMode(MELBUS_CLOCKBIT, INPUT_PULLUP);
#ifdef SERDBG
  // Initiate serial communication to debug via serial-usb (arduino)
  Serial.begin(230400);
  Serial.println("Initiating contact with Melbus:");
#endif
  // Call function that tells HU that we want to register a new device
  melbus_Init_CDCHRG();
}

// Main loop
void loop() {
  // Waiting for the clock interrupt to trigger 8 times to read one byte before
  // evaluating the data
#ifdef SERDBG
  if (ByteIsRead) {
    // Reset bool to enable reading of next byte
    ByteIsRead = false;

    if (incomingByte == ' ') {
      if ( M(11,0x0)
        && (M(10,0x4A) || M(10,0x4C) || M(10,0x4E))
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
        melbus_CharBytes--;
      } else {
        Serial.print(_M[1],HEX);
        Serial.write(' ');
      }
    }
  }
#endif

  // If BUSYPIN is HIGH => HU is in between transmissions
  if (digitalRead(MELBUS_BUSY)==HIGH) {
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
      melbus_SendCnt=0;
      melbus_DiscCnt=0;
      DDRD &= ~(1<<MELBUS_DATA);
      PORTD |= (1<<MELBUS_DATA);
    }
  }
#ifdef SERDBG
  if (Serial.available() > 0) {
    // read the incoming byte:
    incomingByte = Serial.read();
  }
  if (incomingByte == 'i') {
    melbus_Init_CDCHRG();
    Serial.println("\n forced init: ");
    incomingByte=0;
  }
#endif
  if ((melbus_Bitposition == 0x80) && (PIND & (1<<MELBUS_CLOCKBIT))) {
    delayMicroseconds(7);
    DDRD &= ~(1<<MELBUS_DATA);
    PORTD |= (1<<MELBUS_DATA);
  }
}

int main(void) {
  // init();

  setup();

  for (;;) {
    loop();
    if (serialEventRun) serialEventRun();
  }

  return 0;
}
