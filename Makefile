CC=clang

CFLAGS=\
	-std=c11 \
	-fshort-enums \
	-fsanitize=address \
	-fsanitize=undefined \
	-g \
	-W \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wimplicit-fallthrough \
	-Wimplicit-int-conversion \
	-Wshadow \
	-Wstrict-prototypes \
	-Wmissing-prototypes

NAME=minic
BUILD_DIR=out
HEADERS=$(wildcard *.h)
SOURCES=$(wildcard *.c)
OBJECTS=$(addprefix $(BUILD_DIR)/, $(SOURCES:.c=.o))

all: $(BUILD_DIR)/$(NAME) tidy

$(BUILD_DIR)/$(NAME): $(OBJECTS)
	@ mkdir -p $(BUILD_DIR)
	@ $(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.c $(HEADERS)
	@ mkdir -p $(BUILD_DIR)
	@ $(CC) $(CFLAGS) -c -o $@ $<

tidy: $(HEADERS) $(SOURCES)
	@ clang-format -i $^

test: all
	./test.sh

clean:
	@ rm -r $(BUILD_DIR)

.PHONY: all tidy test clean
