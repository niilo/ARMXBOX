CC ?= cc
.DEFAULT_GOAL := swizzle-test
CFLAGS ?= -O2 -Wall -g
SWIZZLE_SOURCE ?= ../../../hw/xbox/nv2a/pgraph/swizzle.c

OBJECTS = swizzle-test.o swizzle-a.o

# Optional original source for interleaved before/after measurements.
ifneq ($(BASELINE_SOURCE),)
OBJECTS += swizzle-b.o
swizzle-test.o: CPPFLAGS += -DSWIZZLE_COMPARE_BASELINE
swizzle-b.o: $(BASELINE_SOURCE)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dswizzle_box=swizzle_box_B \
		-Dunswizzle_box=unswizzle_box_B \
		-Dswizzle_box_internal=swizzle_box_B_internal \
		-Dunswizzle_box_internal=unswizzle_box_B_internal -c $< -o $@
endif

swizzle-test: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

swizzle-test.o: swizzle-test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

swizzle-a.o: $(SWIZZLE_SOURCE)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dswizzle_box=swizzle_box_A \
		-Dunswizzle_box=unswizzle_box_A \
		-Dswizzle_box_internal=swizzle_box_A_internal \
		-Dunswizzle_box_internal=unswizzle_box_A_internal -c $< -o $@

.PHONY: check bench clean
check: swizzle-test
	./swizzle-test --check

bench: swizzle-test
	./swizzle-test --bench

clean:
	rm -f swizzle-test swizzle-test.o swizzle.o swizzle-a.o swizzle-b.o
