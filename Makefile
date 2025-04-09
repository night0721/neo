.POSIX:

VERSION = 1.0
TARGET = neo
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

CFLAGS += -std=c99 -pedantic -Wall -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700

.c.o:
	$(CC) -o $@ $(CFLAGS) -c $<

$(TARGET): $(TARGET).o
	$(CC) -o $@ $(TARGET).o

dist:
	mkdir -p $(TARGET)-$(VERSION)
	cp -R README.md $(TARGET) $(TARGET)-$(VERSION)
	tar -czf $(TARGET)-$(VERSION).tar.gz $(TARGET)-$(VERSION)
	$(RM) -r $(TARGET)-$(VERSION)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -p $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	$(RM) $(TARGET) *.o


.PHONY: all dist install uninstall clean
