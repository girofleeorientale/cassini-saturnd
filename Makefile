UTIL_FILES := src/requests.c src/timing-text-io.c src/utils.c src/tasks.c src/requests-handler.c
FLAGS :=

all: distclean cassini saturnd

cassini: src/cassini.c include/cassini.h
	gcc -I include -Wall $(FLAGS) $(UTIL_FILES) src/cassini.c -o cassini -lm
saturnd : src/saturnd.c
	gcc -I include -Wall $(FLAGS) $(UTIL_FILES) src/saturnd.c -o saturnd -lm
distclean:
	rm cassini saturnd tmpdaemon *.out; true
