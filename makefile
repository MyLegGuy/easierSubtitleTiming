src = $(wildcard *.c)
obj = $(src:.c=.o)

LDFLAGS = -lSDLFontCache -lSDL2_ttf -lsndfile -lSDL2 -lm
CFLAGS = -g -Wall
OUTNAME = a.out

$(OUTNAME): $(obj)
	$(CC) -o $(OUTNAME) $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) $(OUTNAME)
