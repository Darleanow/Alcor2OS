# Host-compiled kernel unit tests (see tests/).

.PHONY: test coverage test-clean

test:
	$(MAKE) -C tests test

coverage:
	$(MAKE) -C tests coverage

test-clean:
	$(MAKE) -C tests clean
