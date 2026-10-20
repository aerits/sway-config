##
# a.c
#
# @file
# @version 0.1

a.c:
	gcc -O3 a.c
dbg:
	clang -Wall -Wextra -fsanitize=address,undefined -g a.c
clean:
	rm a.out

# end
