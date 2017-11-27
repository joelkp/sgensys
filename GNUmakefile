CFLAGS=-std=gnu99 -W -Wall -O2 -ggdb -ffast-math
LFLAGS=-lm
LFLAGS_LINUX=$(LFLAGS) -lasound
LFLAGS_OSSAUDIO=$(LFLAGS) -lossaudio
OBJ=audiodev.o \
    generator.o \
    lexer.o \
    mempool.o \
    osc.o \
    parser.o \
    program.o \
    sgensys.o \
    symtab.o \
    wavfile.o

all: sgensys

clean:
	rm -f $(OBJ) sgensys

sgensys: $(OBJ)
	@if [ `uname -s` == 'Linux' ]; then \
		echo "Linking for Linux build (ALSA and OSS)."; \
		$(CC) $(LFLAGS_LINUX) $(OBJ) -o sgensys; \
	else \
		echo "Linking for generic OSS build."; \
		$(CC) $(LFLAGS_OSSAUDIO) $(OBJ) -o sgensys; \
	fi

audiodev.o: audiodev.c audiodev_*.c sgensys.h audiodev.h
	$(CC) -c $(CFLAGS) audiodev.c

generator.o: generator.c sgensys.h math.h osc.h program.h
	$(CC) -c $(CFLAGS) generator.c

lexer.o: lexer.c sgensys.h symtab.h lexer.h math.h
	$(CC) -c $(CFLAGS) lexer.c

mempool.o: mempool.c sgensys.h mempool.h
	$(CC) -c $(CFLAGS) mempool.c

osc.o: osc.c sgensys.h math.h osc.h
	$(CC) -c $(CFLAGS) osc.c

parser.o: parser.c sgensys.h math.h parser.h program.h
	$(CC) -c $(CFLAGS) parser.c

program.o: program.c sgensys.h parser.h program.h
	$(CC) -c $(CFLAGS) program.c

sgensys.o: sgensys.c sgensys.h audiodev.h wavfile.h
	$(CC) -c $(CFLAGS) sgensys.c

symtab.o: symtab.c sgensys.h symtab.h mempool.h
	$(CC) -c $(CFLAGS) symtab.c

wavfile.o: wavfile.c sgensys.h wavfile.h
	$(CC) -c $(CFLAGS) wavfile.c