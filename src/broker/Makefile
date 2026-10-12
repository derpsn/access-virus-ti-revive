# Broker core + hardware probe. make -C src/broker      -> Linux (libusb) and Windows (WinUSB, mingw-w64)
CFLAGS = -O2 -Wall -Wextra
OUT = ../../work/build
SRC_MIDI = vticore_midi.c

all: $(OUT)/linux/vtiprobe $(OUT)/linux/vtibroker $(OUT)/vtiprobe.exe $(OUT)/vtisend.exe $(OUT)/vtiflash.exe $(OUT)/vtibroker.exe $(OUT)/vtiprobe-pipe.exe $(OUT)/vtiaudiotest.exe

$(OUT)/linux/vtiprobe: vtiprobe.c vticore.c $(SRC_MIDI) vticore.h
	mkdir -p $(OUT)/linux
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libusb-1.0) -o $@ vtiprobe.c vticore.c $(SRC_MIDI) \
		$(shell pkg-config --libs libusb-1.0) -lpthread

# Broker for macOS / Linux on libusb (vtibroker_posix.c); on macOS: make -C src/broker mac (needs libusb from Homebrew)
$(OUT)/linux/vtibroker: vtibroker_posix.c vticore.c $(SRC_MIDI) vticore.h vtiproto.h
	mkdir -p $(OUT)/linux
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libusb-1.0) -o $@ vtibroker_posix.c vticore.c $(SRC_MIDI) \
		$(shell pkg-config --libs libusb-1.0) -lpthread

$(OUT)/vtiprobe.exe: vtiprobe.c vticore_winusb.c $(SRC_MIDI) vticore.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtiprobe.c vticore_winusb.c $(SRC_MIDI) -lwinusb -lsetupapi -static -lpthread

$(OUT)/vtisend.exe: vtisend.c vticore_winusb.c $(SRC_MIDI) vticore.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtisend.c vticore_winusb.c $(SRC_MIDI) -lwinusb -lsetupapi -static -lpthread

$(OUT)/vtiflash.exe: vtiflash.c vticore_winusb.c $(SRC_MIDI) vticore.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtiflash.c vticore_winusb.c $(SRC_MIDI) -lwinusb -lsetupapi -static -lpthread

# Broker (owns WinUSB) and a probe that goes through the broker instead of WinUSB
$(OUT)/vtibroker.exe: vtibroker.c vticore_winusb.c $(SRC_MIDI) vticore.h vtiproto.h vtiaudioshm.h ../wrapper/vtiusbaudio.c ../wrapper/vtiusbaudio.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtibroker.c vticore_winusb.c $(SRC_MIDI) ../wrapper/vtiusbaudio.c -lwinusb -lsetupapi -static -lpthread

$(OUT)/vtiprobe-pipe.exe: vtiprobe.c vticore_pipe.c $(SRC_MIDI) vticore.h vtiproto.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtiprobe.c vticore_pipe.c $(SRC_MIDI) -static -lpthread

clean:
	rm -f $(OUT)/linux/vtiprobe $(OUT)/vtiprobe.exe $(OUT)/vtisend.exe $(OUT)/vtibroker.exe $(OUT)/vtiprobe-pipe.exe

# Test client of the broker audio (protocol 2): vtiaudiotest [seconds]
$(OUT)/vtiaudiotest.exe: vtiaudiotest.c vticore_pipe.c $(SRC_MIDI) vticore.h vtiproto.h vtiaudioshm.h
	mkdir -p $(OUT)
	x86_64-w64-mingw32-gcc $(CFLAGS) -o $@ vtiaudiotest.c vticore_pipe.c $(SRC_MIDI) -static -lpthread

# macOS (run on the Mac): native broker (a separate process, so arm64 is fine; only our libVirusUSB.dylib
# replacement must be x86_64 for the Rosetta DAW); libusb from Homebrew (brew install libusb pkg-config)
mac: vtibroker_posix.c vtimidi_mac.c vticore.c $(SRC_MIDI) vticore.h vtiproto.h vtimidi_mac.h
	mkdir -p $(OUT)/mac
	clang $(CFLAGS) $$(pkg-config --cflags libusb-1.0) -o $(OUT)/mac/vtibroker vtibroker_posix.c vtimidi_mac.c vticore.c \
		$(SRC_MIDI) $$(pkg-config --libs libusb-1.0) -lpthread -framework CoreFoundation
