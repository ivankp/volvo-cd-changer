CC = avr-gcc
DF = -Isrc
CF = -DF_CPU=16000000UL -mmcu=atmega328p -Wall -fmax-errors=3 -Os -flto -Isrc
LF = -mmcu=atmega328p -flto
AVRDUDE = $(shell which avrdude)
AVRDUDE_FLAGS = -F -V -c arduino -p ATMEGA328P -P /dev/ttyACM0 -b 115200

.PHONY: all clean write read

GREP_MAIN := egrep -rl '^ *int +main *\(' --include='*.c' src
HEX := $(patsubst src/%.c,%.hex,$(shell $(GREP_MAIN)))

ifeq (0, $(words $(findstring $(MAKECMDGOALS), clean read)))

SRCS = $(shell find src -type f -name '*.c')
OBJS = $(patsubst src/%.c,.build/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

all: $(HEX)

ifneq (0, $(words $(findstring $(MAKECMDGOALS), write)))
ifneq (1, $(words $(HEX)))
$(error "hex files: $(HEX)")
endif
endif

-include $(DEPS)

.SECONDEXPANSION:

$(DEPS): .build/%.d: src/%.c | .build/$$(dir %)
	$(CC) $(DF) -MM -MT '$(@:.d=.o)' $< -MF $@

.build/%.o:
	$(CC) $(CF) $(C_$*) -c $(filter %.c,$^) -o $@

.build/%.elf: .build/%.o
	$(CC) $(LF) $(filter %.o,$^) -o $@ $(L_$*)

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

