#Change output_file_name to your desired exe filename

#Set the compiler you are using ("gcc" for C or "g++" for C++)
CC=g++

#Set any compiler flags you want to use (e.g. "-I."), or leave blank
CFLAGS = -g -O2 -Wall -std=gnu++11 -fstack-protector-all -Wextra -I.

ifeq ($(OS),Windows_NT)
LIBS = -lpthread -lwsock32
else
LIBS = -lpthread
endif

#Set any dependent files (e.g. header files) so that if they are edited they cause a re-compile (e.g. "main.h my_sub_functions.h some_definitions_file.h"), or leave blank
DEPS = vbit2.h service.h configure.h pagelist.h ttxpage.h packet.h tables.h mag.h

ifeq ($(OS),Windows_NT)
OBJ = strcasestr.o vbit.o packet.o tables.o stream.o mag.o buffer.o page.o outputstream.o HandleTCPClient.o delay.o hamm.o nu4.o thread.o settings.o
else
OBJ = vbit2.o service.o configure.o pagelist.o ttxpage.o packet.o tables.o mag.o
# packet.o tables.o stream.o mag.o buffer.o page.o outputstream.o HandleTCPClient.o delay.o hamm.o nu4.o thread.o settings.o
endif

#Below here doesn't need to change
#Compile each object file
%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

#Combine them into the output file
vbit2: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

#Cleanup
.PHONY: clean

clean:
	rm -f *.o *~ core *~