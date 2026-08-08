CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -I.

SRC = \
	src/vted_terminal.c \
	src/vted.c

TARGET = vted

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

.PHONY: all banner cleanc

all: banner
	@printf "$(GREEN)$(BOLD)Building VTED...$(RESET)\n\n"
	@printf "$(CYAN)Compiling:$(RESET) $(SRC)\n"
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)
	@printf "\n"
	@printf "$(GREEN)$(BOLD)✓ Build complete$(RESET) -> $(CYAN)$(TARGET)$(RESET)\n"

banner:
	@printf "\n"
	@printf "$(CYAN)$(BOLD)"
	@printf "██╗   ██╗████████╗███████╗██████╗ \n"
	@printf "██║   ██║╚══██╔══╝██╔════╝██╔══██╗\n"
	@printf "██║   ██║   ██║   █████╗  ██║  ██║\n"
	@printf "╚██╗ ██╔╝   ██║   ██╔══╝  ██║  ██║\n"
	@printf " ╚████╔╝    ██║   ███████╗██████╔╝\n"
	@printf "  ╚═══╝     ╚═╝   ╚══════╝╚═════╝"
	@printf "$(RESET)  $(GRAY)Makefile v1.0$(RESET)\n"
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