.POSIX:

VERSION = 1.0
TARGET = neo
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

CFLAGS += -std=c99 -pedantic -Wall -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700

$(TARGET): $(TARGET).c
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $(TARGET).c

dist:
	mkdir -p $(TARGET)-$(VERSION)
	cp -R README.md $(TARGET) $(TARGET)-$(VERSION)
	tar -czf $(TARGET)-$(VERSION).tar.gz $(TARGET)-$(VERSION)
	rm -rf $(TARGET)-$(VERSION)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -p $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

all: $(TARGET)

.PHONY: all dist install uninstall clean
