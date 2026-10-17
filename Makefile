CC = gcc

CFLAGS = -Wall -Wextra -Wconversion -std=c11 -I.

SRC = \
	src/vted_terminal.c \
	src/vted.c \
	src/vted_keyboard.c \
	src/vted_screen.c \
	src/vted_row.c \
	src/vted_edit.c \
	src/vted_undo.c \
	src/vted_file.c \
	src/vted_utf8.c

TARGET = vted

# Version & build date
VERSION = 0.1.0
DATE := $(shell date +%Y-%-m-%-d)

# Colors
RESET  = \033[0m
BOLD   = \033[1m

RED    = \033[31m
GREEN  = \033[32m
YELLOW = \033[33m
BLUE   = \033[34m
CYAN   = \033[36m
WHITE  = \033[37m
GRAY   = \033[90m

# Brown gradient (256-color) — dark to light
BR1 = \033[38;5;58m
BR2 = \033[38;5;94m
BR3 = \033[38;5;130m
BR4 = \033[38;5;136m
BR5 = \033[38;5;172m
BR6 = \033[38;5;178m

.PHONY: all banner clean install uninstall test

all: banner
	@printf "$(GREEN)$(BOLD)Building VTED...$(RESET)\n\n"
	@printf "$(CYAN)Compiling:$(RESET) $(SRC)\n"
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	@printf "\n"
	@printf "$(GREEN)$(BOLD)✓ Build complete$(RESET) -> $(CYAN)$(TARGET)$(RESET)\n"

banner:
	@printf "\n"
	@printf "$(BOLD)"
	@printf "$(BR1)██╗   ██╗████████╗███████╗██████╗ \n"
	@printf "$(BR2)██║   ██║╚══██╔══╝██╔════╝██╔══██╗\n"
	@printf "$(BR3)██║   ██║   ██║   █████╗  ██║  ██║\n"
	@printf "$(BR4)╚██╗ ██╔╝   ██║   ██╔══╝  ██║  ██║\n"
	@printf "$(BR5) ╚████╔╝    ██║   ███████╗██████╔╝\n"
	@printf "$(BR6)  ╚═══╝     ╚═╝   ╚══════╝╚═════╝"
	@printf "$(RESET)  $(GRAY)v$(VERSION) - $(DATE)$(RESET)\n"
	@printf "\n"

	@printf "$(YELLOW)PLATFORM     $(GRAY)|$(RESET) Linux\n"
	@printf "$(YELLOW)ARCH         $(GRAY)|$(RESET) x86_64\n"
	@printf "$(YELLOW)CC           $(GRAY)|$(RESET) $(CC)\n"
	@printf "$(YELLOW)CFLAGS       $(GRAY)|$(RESET) $(CFLAGS)\n"
	@printf "$(YELLOW)TARGET       $(GRAY)|$(RESET) $(TARGET)\n"
	@printf "$(YELLOW)SOURCES      $(GRAY)|$(RESET) $(words $(SRC)) files\n"
	@printf "\n"

clean:
	@printf "$(YELLOW)Cleaning VTED...$(RESET)\n"
	rm -f $(TARGET)
	rm -rf tests/bin
	@printf "$(GREEN)✓ Clean complete.$(RESET)\n"
# Install locations (override with: make PREFIX=$HOME/.local install)
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

install: all
	@printf "$(CYAN)Installing VTED...$(RESET)\n"
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 644 docs/vted.1 $(DESTDIR)$(MANDIR)/vted.1
	@printf "$(GREEN)$(BOLD)✓ Installed$(RESET) -> $(DESTDIR)$(BINDIR)/$(TARGET)\n"

uninstall:
	@printf "$(YELLOW)Uninstalling VTED...$(RESET)\n"
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/vted.1
	@printf "$(GREEN)✓ Uninstalled.$(RESET)\n"

# Unit tests: every tests/test_*.c is its own program, linked against
# all modules except the entry point in src/vted.c
LIB_SRC = $(filter-out src/vted.c,$(SRC))
TEST_BINS = $(patsubst tests/%.c,tests/bin/%,$(wildcard tests/test_*.c))

tests/bin/%: tests/%.c tests/vted_test.h $(LIB_SRC)
	@mkdir -p tests/bin
	$(CC) $(CFLAGS) $< $(LIB_SRC) -o $@

test: $(TEST_BINS)
	@printf "$(CYAN)Running unit tests:$(RESET)\n"
	@for test_bin in $(TEST_BINS); do ./$$test_bin || exit 1; done
	@printf "$(GREEN)$(BOLD)✓ All tests passed$(RESET)\n"
