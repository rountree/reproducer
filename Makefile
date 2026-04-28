CC = gcc
CFLAGS = -Wall -Wextra -fPIC $(shell pkg-config --cflags flux-core)
LDFLAGS = -shared $(shell pkg-config --libs flux-core)

PLUGIN = flux-event-watch-test.so

all: $(PLUGIN)

$(PLUGIN): flux-event-watch-test.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(PLUGIN)

install: $(PLUGIN)
	mkdir -p $(DESTDIR)/usr/lib/flux/shell
	cp $(PLUGIN) $(DESTDIR)/usr/lib/flux/shell/

.PHONY: all clean install
