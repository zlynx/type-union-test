OPTFLAGS = -Ofast -flto
CFLAGS = -Wall -W -pedantic -g $(OPTFLAGS)
CXXFLAGS = $(CFLAGS) -std=c++17 

type-union-test:

clean:
	rm type-union-test

.PHONY: clean
