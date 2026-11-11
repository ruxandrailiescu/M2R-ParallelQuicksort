all: parallelQuicksort

parallelQuicksort: parallelQuicksort.o

PEDANTIC_PARANOID_FREAK =       -g -Wall -Wshadow -Wcast-align \
				-Waggregate-return -Wmissing-prototypes -Wmissing-declarations \
				-Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
				-Wmissing-noreturn  \
				-Wpointer-arith -Wwrite-strings -finline-functions -O0

REASONABLY_CAREFUL_DUDE =	-g -Wall -O2
NO_PRAYER_FOR_THE_WICKED =	-w -O3
# Benchmark with optimised code by default; debug with
#   make clean all WARNINGS='$(PEDANTIC_PARANOID_FREAK)'
WARNINGS = 			$(REASONABLY_CAREFUL_DUDE)
CFLAGS = $(WARNINGS) -pthread -std=c99
# -lrt is needed for clock_gettime on old glibc only, and does not exist on macOS
LIBS = $(if $(filter Linux,$(shell uname -s)),-lrt)

%: %.o
	$(CC) $(INCLUDES) $(DEFS) $(CFLAGS) $^ $(LIBS) -o $@ 

%.o: %.c
	$(CC) $(INCLUDES) $(DEFS) $(CFLAGS) -c -o $@ $<

.PHONY: all clean
clean:
	rm -f parallelQuicksort *.o *~
