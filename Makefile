CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -I.

SRC = \
	src/vted_terminal.c \
	src/vted.c \
	src/vted_keyboard.c \
	src/vted_screen.c

TARGET = vted

# Version & build date
VERSION = 0.1
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

.PHONY: all banner cleanc

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
	@printf "$(GREEN)✓ Clean complete.$(RESET)\n"