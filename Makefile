
CFLAGS=-O0 -g -Wall -Wextra -Weffc++ -pedantic
LDFLAGS=-lfltk2 -lfltk2_images -lpng -ljpeg

all: image-editor

image-editor: main.o
	g++ $(LDFLAGS) $< -o $@

%.o: %.cpp
	g++ -c $(CFLAGS) $<

clean:
	rm -fr *.o *core*

.PHONY: clean

