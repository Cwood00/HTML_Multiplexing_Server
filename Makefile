all: server.c
	gcc -fsanitize=address -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Werror -std=c17 -Wpedantic -O0 -g server.c -o server

clean:
	rm -rf *.o hw4 d
