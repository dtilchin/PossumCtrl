#include <Arduino.h>
#include <ResponsiveAnalogRead.h>
#include <MIDIUSB.h>
#include <Wire.h>
#include <Bounce2.h>
#include <Adafruit_MCP23017.h>
#include <PCA9956.h>
#include <Mux.h>


#define MCP_ADDR_1 0x04
#define MCP_ADDR_2 0x06
#define PCA_ADDR_1 0x0B
#define PCA_ADDR_2 0x0D

#define MIDI_CHANNEL 8
#define TRACK_COUNT_CC 126
#define POT_CC_BASE 20
#define BUTTON_CC_BASE 50
#define MASTER_PLAY_CC 110
#define MASTER_REC_CC 111
#define MASTER_POT_1_CC 115
#define MASTER_POT_2_CC 116
#define MASTER_POT_3_CC 117
#define MASTER_POT_4_CC 118

#define LED_LOW 16
#define LED_HI 255


Adafruit_MCP23017 mcp1;
Adafruit_MCP23017 mcp2;
Adafruit_MCP23017* mcps[] = { &mcp1, &mcp2 };

PCA9956 pca1(&Wire);
PCA9956 pca2(&Wire);
PCA9956* pcas[] = { &pca1, &pca2 };

admux::Mux mux(admux::Pin(A0, INPUT, admux::PinType::Analog), admux::Pinset(8, 9, 10));




class MuxedButton : public Bounce2::Button {
  public:
    MuxedButton() {};

    void init(Adafruit_MCP23017* mcp, int pin) {
      this->mcp = mcp;
      this->attach(pin, INPUT);
      this->mcp->pinMode(pin, INPUT);
      this->mcp->pullUp(pin, 1);
      this->setPressedState(0);
    }

  protected:
    virtual void setPinMode(int pin, int mode) override {};

    virtual bool readCurrentState() override {
      return this->mcp->digitalRead(pin);
    }
  
  private:
    Adafruit_MCP23017* mcp;
};



class Input {
  public:
    uint8_t cc;

    Input(uint8_t cc) : cc(cc) {}

    virtual void init() = 0;
    virtual void emit() = 0;
    virtual void receive(uint8_t value) = 0;
};



template <class T>
class LEDButtonBase: public Input {
  public:
    LEDButtonBase(uint8_t cc, uint8_t pcaInd, uint8_t pcaPin)
      : Input(cc), pcaInd(pcaInd), pcaPin(pcaPin), btn() {}
  
    void emit() override {
      // Buttons act as a momentary toggle in Live; just send 127 if it has been pressed
      if (this->btn.update() && this->btn.isPressed()) {
        usbMIDI.sendControlChange(this->cc, 127, MIDI_CHANNEL);
      }
    }

    void receive(uint8_t value) override {
      pcas[pcaInd]->onLED(pcaPin);
      // value > 0 ? pcas[pcaInd]->onLED(pcaPin) : pcas[pcaInd]->offLED(pcaPin);
      value > 0 ? pcas[pcaInd]->pwmLED(pcaPin, LED_HI) : pcas[pcaInd]->pwmLED(pcaPin, LED_LOW);
    }

  protected:
    uint8_t pcaInd;
    uint8_t pcaPin;
    T btn;
};


class LEDButton : public LEDButtonBase<Bounce2::Button> {
  public:
    LEDButton(uint8_t cc, uint8_t pin, uint8_t pcaInd, uint8_t pcaPin)
      : LEDButtonBase(cc, pcaInd, pcaPin), pin(pin) {}

    void init() override {
      this->btn.attach(pin, INPUT_PULLUP);
    }

  private:
    uint8_t pin;
};


class LEDMuxedButton : public LEDButtonBase<MuxedButton> {
  public:
    LEDMuxedButton(uint8_t cc, uint8_t mcpInd, uint8_t mcpPin, uint8_t pcaInd, uint8_t pcaPin)
      : LEDButtonBase(BUTTON_CC_BASE + cc, pcaInd, pcaPin), mcpInd(mcpInd), mcpPin(mcpPin) {}
    
    void init() override {
      this->btn.init(mcps[mcpInd], this->mcpPin);
    }
    
  private:
    uint8_t mcpInd;
    uint8_t mcpPin;
};


class Pot : public Input {
  public:
    Pot(uint8_t cc, uint8_t adcPin)
      : Input(cc), reader(adcPin, false) {}
    

    void init() override {}
    void receive(uint8_t value) override {}

    void emit() override {
      reader.update();

      int value = constrain(this->reader.getValue(), 0, 1023) / 8;
      if (abs(value - this->lastValue) <= minChangeToSend) {
        return;
      }
      usbMIDI.sendControlChange(this->cc, value, MIDI_CHANNEL);
      this->lastValue = value;
    }
  
  private:
    int lastValue;
    ResponsiveAnalogRead reader;

    static const int minChangeToSend = 2;
};


class MuxedPot : public Pot {
  public:
    MuxedPot(uint8_t cc, uint8_t adcPin, int muxChannel)
      : Pot(POT_CC_BASE + cc, adcPin), muxChannel(muxChannel) {}

    void emit() override {
      mux.channel(muxChannel);
      Pot::emit();
    }

  private:
    int muxChannel;
};



extern "C" char* sbrk(int incr);
int freeRam() {
  char top;
  return &top - reinterpret_cast<char*>(sbrk(0));
}


Input* controls[] = {
  // U1 0x04 / U3 0x0B
  new LEDMuxedButton(0, 0, 0, 0, 19),  // S8
  new LEDMuxedButton(1, 0, 1, 0, 18),  // M8
  new LEDMuxedButton(2, 0, 2, 0, 17),  // S7
  new LEDMuxedButton(3, 0, 3, 0, 16),  // M7
  new LEDMuxedButton(4, 0, 4, 0, 15),  // S6
  new LEDMuxedButton(5, 0, 5, 0, 14),  // M6
  new LEDMuxedButton(6, 0, 6, 0, 13),  // S5
  new LEDMuxedButton(7, 0, 7, 0, 12),  // M5
  new LEDMuxedButton(8, 0, 8, 0, 11),  // S4
  new LEDMuxedButton(9, 0, 9, 0, 10),  // M4
  new LEDMuxedButton(10, 0, 10, 0, 9), // S3
  new LEDMuxedButton(11, 0, 11, 0, 8), // M3
  new LEDMuxedButton(12, 0, 12, 0, 7), // S2
  new LEDMuxedButton(13, 0, 13, 0, 6), // M2
  new LEDMuxedButton(14, 0, 14, 0, 5), // S1
  new LEDMuxedButton(15, 0, 15, 0, 4), // M1

  // U2 0x06 / U4 0x0D
  new LEDMuxedButton(16, 1, 0, 1, 23), // R8
  new LEDMuxedButton(17, 1, 1, 1, 22), // P8
  new LEDMuxedButton(18, 1, 2, 1, 21), // R7
  new LEDMuxedButton(19, 1, 3, 1, 20), // P7
  new LEDMuxedButton(20, 1, 4, 1, 19), // R6
  new LEDMuxedButton(21, 1, 5, 1, 18), // P6
  new LEDMuxedButton(22, 1, 6, 1, 17), // R5
  new LEDMuxedButton(23, 1, 7, 1, 16), // P5
  new LEDMuxedButton(24, 1, 8, 1, 7),  // R4
  new LEDMuxedButton(25, 1, 9, 1, 6),  // P4
  new LEDMuxedButton(26, 1, 10, 1, 5), // R3
  new LEDMuxedButton(27, 1, 11, 1, 4), // P3
  new LEDMuxedButton(28, 1, 12, 1, 3), // R2
  new LEDMuxedButton(29, 1, 13, 1, 2), // P2
  new LEDMuxedButton(30, 1, 14, 1, 1), // R1
  new LEDMuxedButton(31, 1, 15, 1, 0), // P1

  // U5 / A8 ("ADC0")
  new MuxedPot(0, A8, 5), // G1
  new MuxedPot(1, A8, 7), // G2
  new MuxedPot(2, A8, 6), // G3
  new MuxedPot(3, A8, 4), // G4
  // U6 / A9 ("ADC1")
  new MuxedPot(4, A9, 5), // G5
  new MuxedPot(5, A9, 7), // G6
  new MuxedPot(6, A9, 6), // G7
  new MuxedPot(7, A9, 4), // G8

  // U5 / A8 ("ADC0")
  new MuxedPot(8, A8, 3), // U1
  new MuxedPot(9, A8, 0), // U2
  new MuxedPot(10, A8, 1), // U3
  new MuxedPot(11, A8, 2), // U4
  // U6 / A9 ("ADC1")
  new MuxedPot(12, A9, 3), // U5
  new MuxedPot(13, A9, 0), // U6
  new MuxedPot(14, A9, 1), // U7
  new MuxedPot(15, A9, 2), // U8

  // Master controls
  new LEDButton(MASTER_REC_CC, 12, 1, 12), // R_MASTER
  new LEDButton(MASTER_PLAY_CC, 13, 1, 13), // P_MASTER
  new Pot(MASTER_POT_1_CC, A3), // FX_1
  new Pot(MASTER_POT_2_CC, A2), // FX_2
  new Pot(MASTER_POT_3_CC, A0), // FX_3
  new Pot(MASTER_POT_4_CC, A1), // FX_4
};


void handleCc(uint8_t channel, uint8_t control, uint8_t value) {
  if (control == TRACK_COUNT_CC) {
    Serial.print("Got track count CC: ");
    Serial.println(value);
    return;
  }

  for (auto c : controls) { 
    if (c->cc == control) {
      c->receive(value);
    }
  }
}


void setup() {
  Serial.begin(9600);

  // while (!Serial && millis() < 5000) {}
  // delay(500);

  Serial.printf("Init (%d bytes free)\n", freeRam());

  Wire.begin();

  mcp1.begin(MCP_ADDR_1);
  mcp2.begin(MCP_ADDR_2);

  pca1.init(PCA_ADDR_1, 0x06, true);
  pca2.init(PCA_ADDR_2, 0x06, true);

  uint8_t pattern[PCA9965_NUM_LEDS];
  for (size_t i = 0; i < PCA9965_NUM_LEDS; i++) {
    pattern[i] = LED_LOW;
  }
  pca1.setLEDPattern(pattern);
  pca2.setLEDPattern(pattern);

  usbMIDI.setHandleControlChange(handleCc);

  for (auto c : controls) {
    c->init();
  }

  Serial.printf("Init complete (%d bytes free)\n", freeRam());
}


void loop() {
  while(usbMIDI.read()) {}

  for (auto c : controls) {
    c->emit();
  }

  delay(5);
}
