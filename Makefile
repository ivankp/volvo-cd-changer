CXX = avr-g++
DF = -Isrc
CF = -DF_CPU=16000000UL -mmcu=atmega328p -Wall -fmax-errors=3 -Os -flto -Isrc
LF = -mmcu=atmega328p -flto
AVRDUDE = $(shell which avrdude)
AVRDUDE_FLAGS = -F -V -c arduino -p ATMEGA328P -P /dev/ttyACM0 -b 115200

.PHONY: all clean write read

GREP_MAIN := egrep -rl '^ *int +main *\(' --include='*.cpp' src
HEX := $(patsubst src/%.cpp,%.hex,$(shell $(GREP_MAIN)))

ifeq (0, $(words $(findstring $(MAKECMDGOALS), clean read)))

SRCS = $(shell find src -type f -name '*.cpp')
OBJS = $(patsubst src/%.cpp,.build/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

ARDUINO = /home/ivanp/Desktop/arduino-1.8.5/hardware/arduino/avr

C_main := \
  -I$(ARDUINO)/cores/arduino \
  -I$(ARDUINO)/variants/standard

all: $(HEX)

ifneq (0, $(words $(findstring $(MAKECMDGOALS), write)))
ifneq (1, $(words $(HEX)))
$(error "hex files: $(HEX)")
endif
endif

-include $(DEPS)

.build/main.elf: \
  $(ARDUINO)/cores/arduino/HardwareSerial0.cpp \
  $(ARDUINO)/cores/arduino/HardwareSerial.cpp \
  $(ARDUINO)/cores/arduino/Print.cpp

.SECONDEXPANSION:

$(DEPS): .build/%.d: src/%.cpp | .build/$$(dir %)
	$(CXX) $(DF) $(C_$*) -MM -MT '$(@:.d=.o)' $< -MF $@

.build/%.o:
	$(CXX) $(CF) $(C_$*) -c $(filter %.cpp,$^) -o $@

.build/%.elf: .build/%.o
	$(CXX) $(CF) $(C_$*) $(LF) $(filter %.o %.c %.cpp,$^) -o $@ $(L_$*)

%.hex: .build/%.elf
	avr-objcopy -O ihex -R .eeprom $< $@

%.s: .build/%.elf
	avr-objdump -d $< > $@

write: $(HEX)
	sudo $(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$<

write_%: %.hex
	sudo $(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$<

.build/%/:
	mkdir -p $@

endif

read:
	sudo $(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:r:$(shell date +%s).hex:i

clean:
	@rm -rfv .build $(HEX)

