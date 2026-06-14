# daft - generative ambient MIDI engine driven by system resources.
# Build rules per AGENTS.md: ISO C99, POSIX, warnings as errors, MISRA analysis.

include config.mk

CFLAGS := -std=c99
CFLAGS += -pedantic
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -Wconversion -Wsign-conversion
CFLAGS += -Wshadow
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
CFLAGS += -Wpointer-arith -Wcast-align -Wcast-qual
CFLAGS += -Wwrite-strings -Wswitch-enum -Wundef
CFLAGS += -Wdouble-promotion -Wformat=2
CFLAGS += -Winit-self -Wmissing-declarations -Wredundant-decls
CFLAGS += -Wunreachable-code
CFLAGS += -fno-common -fno-strict-aliasing

CPPFLAGS := -Iinclude
CPPFLAGS += -D_POSIX_C_SOURCE=200809L

LDLIBS := -lm

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
TARGET := daft

# tools/smfcheck is non-production test support (see tools/README.md);
# it is still built with the full production flag set.
TOOLS := tools/smfcheck

.PHONY: all clean analyze install uninstall

all: $(TARGET) $(TOOLS)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

tools/smfcheck: tools/smfcheck.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tools/smfcheck.c

# Suppressions (specific and justified per AGENTS.md):
#  - misra-c2012-15.5 (single exit point, advisory): deviated project-wide;
#    the mandated status-code error-handling pattern uses guarded early
#    returns. See docs/deviations/DEV-003-misra-15.5.md.
#  - misra-config: analysis-completeness notice, not a code finding. System
#    header constants (EINTR, CLOCK_MONOTONIC, off_t) are not expanded
#    because system includes are not parsed; they are POSIX-defined.
analyze:
	cppcheck --std=c99 --enable=all --inconclusive --force --error-exitcode=1 \
	    --library=posix \
	    --inline-suppr \
	    --suppress=missingIncludeSystem \
	    --suppress=misra-c2012-15.5 \
	    --suppress=misra-config \
	    --check-level=exhaustive \
	    --addon=misra.json \
	    -Iinclude \
	    src include

install: $(TARGET)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -f $(TARGET) $(DESTDIR)$(BINDIR)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f daft.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/daft.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/man1/daft.1

clean:
	rm -f $(OBJ) $(TARGET) $(TOOLS)
