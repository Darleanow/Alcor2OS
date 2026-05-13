SUBDIRS := hello-cpp fleed font-demo
ifneq ($(wildcard ../../thirdparty/ncurses-install/usr/lib/libncurses.a),)
SUBDIRS += ncurses-hello
endif

.PHONY: all clean

all:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d || exit 1; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
