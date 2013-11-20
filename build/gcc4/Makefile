# This will point to the root of the FERRET project
SRCDIR = ../../src
DSTDIR = ../../bin
TMPDIR = ../../tmp

LIBS = -ldl -lpthread
INCLUDES = -I. -I$(SRCDIR) -I$(SRCDIR)/include

CC = gcc
CFLAGS = -g $(INCLUDES) -Wall

.SUFFIXES: .c .cpp


$(TMPDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TMPDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@


c_sources := $(wildcard $(SRCDIR)/*.c)
cpp_sources := $(wildcard $(SRCDIR)/*.cpp)

SRC = $(c_sources) $(cpp_sources)

OBJ = $(addprefix $(TMPDIR)/, $(notdir $(addsuffix .o, $(basename $(SRC)))))

$(DSTDIR)/hamster: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) -lm $(LIBS) -lstdc++

depend:
	makedepend $(CFLAGS) -Y $(SRC)

clean:
	rm -f $(OBJ)
