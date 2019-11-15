CC = gcc
CFLAGS = -Wall -g -ggdb -pg -Ofast
LIBS = -lm

all: dt_hash.c cmds.c bin/predis bin/predis-server bin/parallel-test bin/hashtable-test bin/parallel-hashtable-test

test: bin/set-clean-test bin/update-test

types/names.txt: type_list_gen.sh
	./type_list_gen.sh > types/names.txt

dt_hash.c: types/names.txt
	gperf --readonly-tables --hash-function-name=dt_hash --lookup-function-name=dt_valid types/names.txt > dt_hash.c

cmds.c: cmds.txt
	gperf --readonly-tables cmds.txt > cmds.c

command_parser_hashgen: cmds.c command_parser_hashgen.c
	gcc $(CFLAGS) -o $@ command_parser_hashgen.c

command_parser_hashes.h: command_parser_hashgen
	./command_parser_hashgen > command_parser_hashes.h

command_parser.c: command_parser_hashes.h

PREDIS_DEPS = predis.c types/*.c lib/hashtable.c dt_hash.c

bin/set-clean-test: tests/set-clean-test.c $(PREDIS_DEPS)
	$(CC) $(CFLAGS) $(LIBS) -pthread -o $@ tests/set-clean-test.c predis.c types/*.c lib/hashtable.c

bin/update-test: tests/update-test.c $(PREDIS_DEPS)
	$(CC) $(CFLAGS) $(LIBS) -pthread -o $@ tests/update-test.c predis.c types/*.c lib/hashtable.c

bin/predis: cli.c command_parser.c $(PREDIS_DEPS)
	$(CC) $(CFLAGS) $(LIBS) -o bin/predis cli.c predis.c types/*.c command_parser.c lib/hashtable.c -ledit

bin/predis-server: server.c cmds.c command_parser.c $(PREDIS_DEPS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $< -pthread predis.c lib/hashtable.c types/*.c command_parser.c

serve: bin/predis-server
	$<

ptest: bin/parallel-test
	$<

htest: bin/hashtable-test
	$<

bin/gen_test_file: gen_test_file.c
	$(CC) $(CFLAGS) $(LIBS) -o bin/gen_test_file gen_test_file.c

bin/parallel-test: tests/parallel-test.c lib/random_string.c
	gcc -g -o $@ $< -lhiredis -lpthread lib/random_string.c

bin/parallel-hashtable-test: tests/parallel-hashtable-test.c lib/random_string.c lib/hashtable.c
	gcc -g -o $@ $< -lpthread lib/random_string.c lib/hashtable.c

bin/hashtable-test: tests/hashtable-test.c lib/hashtable.c lib/random_string.c
	gcc -g -o $@ $< lib/hashtable.c lib/random_string.c

clean:
	rm -f bin/* cmds.c types/names.txt dt_hash.c command_parser_hashes.h command_parser_hashgen
