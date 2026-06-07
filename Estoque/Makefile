# =====================================================================
# Makefile — Estoque de Componentes (Windows + MinGW-w64 / clang)
#
# Pré-requisitos:
#   - MinGW-w64 no PATH  (gcc, ar)
#   - raylib compilado para Windows; ajuste RAYLIB_DIR abaixo
#   - raygui.h no mesmo diretório dos fontes (single-header)
#
# Uso:
#   make          → compila em modo Release (estoque.exe)
#   make debug    → compila com símbolos de depuração
#   make clean    → remove todos os objetos e o executável
# =====================================================================

# ---- Configurações ----
TARGET    = estoque.exe
CC        = gcc
RAYLIB_DIR = .          # pasta com raylib.h, raygui.h e libraylib.a

CFLAGS_COMMON = -I$(RAYLIB_DIR) -I. \
                -Wall -Wextra -Wshadow \
                -DPLATFORM_DESKTOP \
                -mwindows

CFLAGS_RELEASE = $(CFLAGS_COMMON) -O2
CFLAGS_DEBUG   = $(CFLAGS_COMMON) -O0 -g -DDEBUG

LDFLAGS = -L$(RAYLIB_DIR) -lraylib \
          -lwinspool -lws2_32 \
          -lopengl32 -lgdi32 -lwinmm \
          -static-libgcc

# ---- Fontes e objetos ----
SRCS = sqlite3.c     \
       db_io.c       \
       app_state.c   \
       utils.c       \
       ui_helpers.c  \
       csv_io.c      \
       printer.c     \
       sales.c       \
       network.c     \
       audit.c       \
       undo.c        \
       app_windows.c

OBJS = $(SRCS:.c=.o)

# ---- Regras ----
.PHONY: all debug clean

all: CFLAGS = $(CFLAGS_RELEASE)
all: $(TARGET)

debug: CFLAGS = $(CFLAGS_DEBUG)
debug: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Binário gerado: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---- Dependências explícitas (evita recompilar o desnecessário) ----
app_state.o:   app_state.c   platform.h app_types.h  app_state.h
utils.o:       utils.c       platform.h utils.h
ui_helpers.o:  ui_helpers.c  platform.h app_config.h app_state.h ui_helpers.h
csv_io.o:      csv_io.c      platform.h app_config.h app_types.h app_state.h utils.h audit.h undo.h csv_io.h
printer.o:     printer.c     platform.h app_config.h app_types.h app_state.h utils.h printer.h
sales.o:       sales.c       platform.h app_config.h app_types.h app_state.h utils.h audit.h sales.h
network.o:     network.c     platform.h app_config.h app_types.h app_state.h \
                              csv_io.h sales.h printer.h network.h
audit.o:       audit.c       platform.h app_config.h app_types.h app_state.h utils.h ui_helpers.h audit.h
undo.o:        undo.c        platform.h app_config.h app_types.h app_state.h undo.h
app_windows.o: app_windows.c platform.h app_config.h app_types.h app_state.h \
                              utils.h ui_helpers.h csv_io.h printer.h sales.h network.h audit.h undo.h

clean:
	-del /f /q $(OBJS) $(TARGET) 2>nul || rm -f $(OBJS) $(TARGET)
	@echo "Limpeza concluída."
