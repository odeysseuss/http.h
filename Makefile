cc := clang
cflags := -std=c11 -Wall -Wextra -pedantic -MMD -MP -D_GNU_SOURCE
incdir := -Ihttp -Iutils
objdir := build/obj

MODE ?= debug
ifeq ($(MODE), release)
	cflags += -O2 -ffast-math -march=native
else
	cflags += -O0 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined,leak
endif

srcs := $(wildcard src/*.c)
objs := $(srcs:src/%.c=$(objdir)/%.o)
deps := $(objs:.o=.d)
exec := build/http
-include $(deps)

.PHONY: all test run clean

all: $(exec)

run: $(exec)
	@./$(exec)

$(exec): $(objs)
	$(cc) $(cflags) $^ -o $@

$(objdir)/%.o: src/%.c | $(objdir)
	$(cc) $(cflags) $(incdir) -c $< -o $@

$(objdir):
	mkdir -p $@

clean:
	rm -rf $(exec) $(objdir)
