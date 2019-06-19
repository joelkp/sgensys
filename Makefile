.POSIX:
CC=cc
CFLAGS=-std=c99 -W -Wall -O2
CFLAGS_FAST=$(CFLAGS) -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
PREFIX=/usr/local
BIN=saugns
SHARE=saugns
OBJ=\
	common.o \
	help.o \
	arrtype.o \
	ptrarr.o \
	mempool.o \
	reflist.o \
	ramp.o \
	wave.o \
	reader/file.o \
	reader/symtab.o \
	reader/scanner.o \
	reader/parser.o \
	reader/parseconv.o \
	builder/scriptconv.o \
	builder/builder.o \
	interp/osc.o \
	interp/mixer.o \
	interp/prealloc.o \
	interp/interp.o \
	player/audiodev.o \
	player/wavfile.o \
	player/player.o \
	saugns.o
TEST1_OBJ=\
	common.o \
	arrtype.o \
	ptrarr.o \
	mempool.o \
	reader/file.o \
	reader/symtab.o \
	reader/scanner.o \
	reader/lexer.o \
	test-scan.o

all: $(BIN)
tests: test-scan
clean:
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST1_OBJ) test-scan
install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/share/$(SHARE)
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin
	cp -RP examples/* $(DESTDIR)$(PREFIX)/share/$(SHARE)
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -Rf $(DESTDIR)$(PREFIX)/share/$(SHARE)

$(BIN): $(OBJ)
	@UNAME="`uname -s`"; \
	if [ $$UNAME = 'Linux' ]; then \
		echo "Linking for Linux (using ALSA and OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_LINUX) -o $(BIN); \
	elif [ $$UNAME = 'OpenBSD' ]; then \
		echo "Linking for OpenBSD (using sndio)."; \
		$(CC) $(OBJ) $(LFLAGS_SNDIO) -o $(BIN); \
	elif [ $$UNAME = 'NetBSD' ]; then \
		echo "Linking for NetBSD (using OSS)."; \
		$(CC) $(OBJ) $(LFLAGS_OSSAUDIO) -o $(BIN); \
	else \
		echo "Linking for UNIX with OSS."; \
		$(CC) $(OBJ) $(LFLAGS) -o $(BIN); \
	fi

test-scan: $(TEST1_OBJ)
	$(CC) $(TEST1_OBJ) $(LFLAGS) -o test-scan

arrtype.o: arrtype.c arrtype.h common.h mempool.h
	$(CC) -c $(CFLAGS) arrtype.c

builder/builder.o: builder/builder.c common.h math.h program.h ptrarr.h ramp.h reflist.h script.h saugns.h time.h wave.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/scriptconv.o: arrtype.h builder/scriptconv.c builder/scriptconv.h common.h math.h mempool.h program.h ptrarr.h ramp.h reflist.h script.h time.h wave.h
	$(CC) -c $(CFLAGS) builder/scriptconv.c -o builder/scriptconv.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

help.o: common.h help.c help.h ramp.h wave.h
	$(CC) -c $(CFLAGS) help.c

interp/interp.o: arrtype.h common.h interp/interp.c interp/interp.h interp/mixer.h interp/osc.h interp/prealloc.h math.h mempool.h program.h ramp.h time.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/interp.c -o interp/interp.o

interp/mixer.o: common.h interp/mixer.c interp/mixer.h math.h ramp.h
	$(CC) -c $(CFLAGS_FAST) interp/mixer.c -o interp/mixer.o

interp/osc.o: common.h interp/osc.c interp/osc.h math.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/osc.c -o interp/osc.o

interp/prealloc.o: arrtype.h common.h interp/interp.h interp/osc.h interp/prealloc.c interp/prealloc.h math.h mempool.h program.h ramp.h time.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/prealloc.c -o interp/prealloc.o

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/player.o: common.h interp/interp.h math.h player/audiodev.h player/player.c player/wavfile.h program.h ptrarr.h ramp.h saugns.h time.h wave.h
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrarr.o: common.h mempool.h ptrarr.c ptrarr.h
	$(CC) -c $(CFLAGS) ptrarr.c

ramp.o: common.h math.h ramp.c ramp.h time.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

reader/file.o: common.h reader/file.c reader/file.h
	$(CC) -c $(CFLAGS) reader/file.c -o reader/file.o

reader/lexer.o: common.h math.h mempool.h reader/file.h reader/lexer.c reader/lexer.h reader/scanner.h reader/symtab.h
	$(CC) -c $(CFLAGS) reader/lexer.c -o reader/lexer.o

reader/parseconv.o: common.h help.h math.h mempool.h program.h ramp.h reader/parseconv.c reader/parser.h reader/symtab.h reflist.h script.h time.h wave.h
	$(CC) -c $(CFLAGS) reader/parseconv.c -o reader/parseconv.o

reader/parser.o: common.h help.h math.h mempool.h program.h ramp.h reader/file.h reader/parser.c reader/parser.h reader/scanner.h reader/symtab.h reflist.h script.h time.h wave.h
	$(CC) -c $(CFLAGS) reader/parser.c -o reader/parser.o

reader/scanner.o: common.h math.h mempool.h reader/file.h reader/scanner.c reader/scanner.h reader/symtab.h
	$(CC) -c $(CFLAGS) reader/scanner.c -o reader/scanner.o

reader/symtab.o: common.h mempool.h reader/symtab.c reader/symtab.h
	$(CC) -c $(CFLAGS) reader/symtab.c -o reader/symtab.o

reflist.o: common.h mempool.h reflist.c reflist.h
	$(CC) -c $(CFLAGS) reflist.c

saugns.o: common.h help.h math.h program.h ptrarr.h ramp.h saugns.c saugns.h time.h wave.h
	$(CC) -c $(CFLAGS) saugns.c

test-scan.o: common.h math.h mempool.h program.h ptrarr.h ramp.h reader/lexer.h reader/scanner.h reader/file.h reader/symtab.h saugns.h test-scan.c time.h wave.h
	$(CC) -c $(CFLAGS) test-scan.c

wave.o: common.h math.h wave.c wave.h
	$(CC) -c $(CFLAGS_FAST) wave.c
