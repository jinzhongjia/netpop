PREFIX  ?= $(HOME)/.local
CC      ?= cc
CFLAGS  ?= -O2 -pipe

PKGS := gtk4 libnm
# gtk4-layer-shell 可选：装了就以 layer-shell 弹出层显示（锚定右上角，不进窗口栈）
ifeq ($(shell pkg-config --exists gtk4-layer-shell-0 && echo yes),yes)
PKGS   := gtk4-layer-shell-0 $(PKGS)
CFLAGS += -DHAVE_LAYER_SHELL
endif

CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
          $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

netpop: netpop.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

install: netpop
	install -Dm755 netpop $(DESTDIR)$(PREFIX)/bin/netpop
	install -Dm644 netpop.desktop $(DESTDIR)$(HOME)/.config/autostart/netpop.desktop

clean:
	rm -f netpop

.PHONY: install clean
