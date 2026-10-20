##
# a.c
#
# @file
# @version 0.1

a.out: a.c
	gcc -O3 a.c
dbg: a.c
	clang -Wall -Wextra -fsanitize=address,undefined -g a.c
clean:
	rm a.out

# end
