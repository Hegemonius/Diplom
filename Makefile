PROG=prog2
CFLAGS=-I

%.o: %.c
	gcc -c -o $@ $<

compile: $(PROG).o netns.o    
	gcc -o $(PROG) $(PROG).o netns.o   
