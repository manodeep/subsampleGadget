UTILS_DIR=.

CC       :=  icc 
CCFLAGS  := -Wextra -Wall -Wformat=3  -g -m64 -std=gnu11  -Wpacked  -Wnested-externs -Wpointer-arith  -Wredundant-decls  -Wfloat-equal -Wcast-qual  -Wshadow  
CCFLAGS  +=  -Wcast-align -Wmissing-declarations -Wmissing-prototypes  -Wnested-externs -fno-strict-aliasing #-D_POSIX_C_SOURCE=2 -Wpadded -Wstrict-prototypes -Wconversion

CCFLAGS  := -Wextra -Wall -Wshadow -g -std=gnu11 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

#OPT := -DUSE_MMAP
OPT := -DUSE_MMAP -DUSE_WRITEV # PWRITEV requires MMAP
#OPT := -DUSE_SENDFILE # sendfile copies between two file descriptors/sockets at kernel level

#### POSIX flag is required for popen in main.c 
UNAME :=$(shell uname -n)
ifneq (,$(findstring utexas,$(UNAME)))
# @TACC
GSL_INCLUDE:=-I$(TACC_GSL_INC) -I$(TACC_GSL_INC)/gsl
GSL_LDFLAGS:=-Wl,-R$(TACC_GSL_LIB) -L$(TACC_GSL_LIB) -lgsl -lgslcblas #-finstrument-functions
else
# Just about every other computer I use
GSL_INCLUDE := $(shell gsl-config --cflags)
GSL_LDFLAGS := $(shell gsl-config --libs)
endif

OPTIMIZE := -axCORE-AVX2 -O3 -qopenmp -xhost

OPTIONS :=  $(OPTIMIZE) $(OPT) $(CCFLAGS)

SOURCES   := main.c $(UTILS_DIR)/progressbar.c $(UTILS_DIR)/utils.c $(UTILS_DIR)/gadget_utils.c
OBJECTS   := $(SOURCES:.c=.o)
INCL      := Makefile progressbar.h utils.h gadget_utils.h gadget_headers.h macros.h

EXECUTABLE = subsample_Gadget_mmap_writev


all: $(SOURCES) $(EXECUTABLE) $(INCL)

$(EXECUTABLE): $(OBJECTS) $(INCL)
	$(CC) $(OPTIONS) $(OBJECTS) -o $@  $(GSL_LDFLAGS) -lrt 

.c.o: $(INCL)
	$(CC) $(GSL_INCLUDE) -I$(UTILS_DIR)  $(OPTIONS) -c $< -o $@


.PHONY: clean clena

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

clena:
	rm -f $(OBJECTS) $(EXECUTABLE)

