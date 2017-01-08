CC = gcc
TARGET = testtts
LIBS = -lsvoxpico -lm

all:
	$(CC) -L../lib/ tts_engine.c main.c $(LIBS) -I../compat/include/ -I../lib/ -o $(TARGET)

clean:
	@/bin/rm $(TARGET)
