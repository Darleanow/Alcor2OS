SUBDIRS := doom

.PHONY: all clean

all:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d all || exit 1; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
