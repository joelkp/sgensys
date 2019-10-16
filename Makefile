.POSIX:
CC=cc
CFLAGS=-std=c99 -W -Wall -O2
CFLAGS_FAST=$(CFLAGS) -ffast-math
LFLAGS=-s -lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_SNDIO=$(LFLAGS) -lsndio
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
PREFIX=/usr/local
BIN=ssndgen
SHARE=ssndgen
OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/parser.o \
	builder/parseconv.o \
	builder/voicegraph.o \
	builder/scriptconv.o \
	builder/builder.o \
	mempool.o \
	ramp.o \
	wave.o \
	interp/osc.o \
	interp/mixer.o \
	interp/prealloc.o \
	interp/generator.o \
	player/audiodev.o \
	player/wavfile.o \
	player/player.o \
	ssndgen.o
TEST_OBJ=\
	common.o \
	arrtype.o \
	ptrlist.o \
	builder/file.o \
	builder/symtab.o \
	builder/scanner.o \
	builder/lexer.o \
	builder/voicegraph.o \
	builder/scriptconv.o \
	mempool.o \
	test-builder.o

all: $(BIN)
test: test-builder
clean:
	rm -f $(OBJ) $(BIN)
	rm -f $(TEST_OBJ) test-builder
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

test-builder: $(TEST_OBJ)
	$(CC) $(TEST_OBJ) $(LFLAGS) -o test-builder

arrtype.o: arrtype.c arrtype.h common.h
	$(CC) -c $(CFLAGS) arrtype.c

builder/builder.o: builder/builder.c builder/file.h common.h math.h program.h ptrlist.h ramp.h script.h ssndgen.h time.h wave.h
	$(CC) -c $(CFLAGS) builder/builder.c -o builder/builder.o

builder/file.o: builder/file.c builder/file.h common.h
	$(CC) -c $(CFLAGS) builder/file.c -o builder/file.o

builder/lexer.o: builder/lexer.c builder/lexer.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/lexer.c -o builder/lexer.o

builder/parseconv.o: builder/parseconv.c builder/parser.h program.h time.h ramp.h wave.h math.h script.h ptrlist.h common.h
	$(CC) -c $(CFLAGS) builder/parseconv.c -o builder/parseconv.o

builder/parser.o: builder/parser.c builder/parser.h builder/file.h builder/symtab.h script.h ptrlist.h program.h time.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) builder/parser.c -o builder/parser.o

builder/scanner.o: builder/scanner.c builder/scanner.h builder/file.h builder/symtab.h math.h common.h
	$(CC) -c $(CFLAGS) builder/scanner.c -o builder/scanner.o

builder/scriptconv.o: builder/scriptconv.c builder/scriptconv.h program.h ramp.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/scriptconv.c -o builder/scriptconv.o

builder/symtab.o: builder/symtab.c builder/symtab.h mempool.h common.h
	$(CC) -c $(CFLAGS) builder/symtab.c -o builder/symtab.o

builder/voicegraph.o: builder/voicegraph.c builder/scriptconv.h program.h time.h ramp.h wave.h math.h script.h ptrlist.h arrtype.h common.h
	$(CC) -c $(CFLAGS) builder/voicegraph.c -o builder/voicegraph.o

common.o: common.c common.h
	$(CC) -c $(CFLAGS) common.c

interp/generator.o: common.h interp/generator.c interp/generator.h interp/mixer.h interp/osc.h interp/prealloc.h math.h mempool.h program.h ramp.h time.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/generator.c -o interp/generator.o

interp/mixer.o: common.h interp/mixer.c interp/mixer.h math.h ramp.h
	$(CC) -c $(CFLAGS_FAST) interp/mixer.c -o interp/mixer.o

interp/osc.o: common.h interp/osc.c interp/osc.h math.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/osc.c -o interp/osc.o

interp/prealloc.o: common.h interp/generator.h interp/osc.h interp/prealloc.c interp/prealloc.h math.h mempool.h program.h ramp.h time.h wave.h
	$(CC) -c $(CFLAGS_FAST) interp/prealloc.c -o interp/prealloc.o

mempool.o: common.h mempool.c mempool.h
	$(CC) -c $(CFLAGS) mempool.c

player/audiodev.o: common.h player/audiodev.c player/audiodev.h player/audiodev/*.c
	$(CC) -c $(CFLAGS) player/audiodev.c -o player/audiodev.o

player/player.o: common.h interp/generator.h math.h player/audiodev.h player/player.c player/wavfile.h program.h ptrlist.h ramp.h ssndgen.h time.h wave.h
	$(CC) -c $(CFLAGS_FAST) player/player.c -o player/player.o

player/wavfile.o: common.h player/wavfile.c player/wavfile.h
	$(CC) -c $(CFLAGS) player/wavfile.c -o player/wavfile.o

ptrlist.o: ptrlist.c ptrlist.h common.h
	$(CC) -c $(CFLAGS) ptrlist.c

ramp.o: ramp.c ramp.h time.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) ramp.c

ssndgen.o: ssndgen.c ssndgen.h ptrlist.h program.h time.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) ssndgen.c

test-builder.o: test-builder.c ssndgen.h builder/lexer.h builder/scanner.h builder/file.h builder/symtab.h ptrlist.h program.h time.h ramp.h wave.h math.h common.h
	$(CC) -c $(CFLAGS) test-builder.c

wave.o: wave.c wave.h math.h common.h
	$(CC) -c $(CFLAGS_FAST) wave.c
