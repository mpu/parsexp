.PONY: all clean

#UDIS = -Llib -ludis86 -DUDIS
BINS = cexp eexp

all: $(BINS)
clean:
	rm -f $(BINS)

%: %.c
	cc -Wall -g $@.c $(UDIS) -o $@
