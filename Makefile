CC=cc -O2 -Wall -Wextra
LIB=
OBJ=st4th.o
BIN=st4th

default: $(OBJ)
	$(CC) -o $(BIN) $(OBJ) $(LIB)

clean:
	rm -f $(BIN) $(OBJ)

