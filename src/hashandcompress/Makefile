#
# Copyright (c) 2017, Mark J. Nelson. All rights reserved.
#
# Makefile for Solidfire's ssfi programming assignment.
#
# Useful targets include:
#
# 	(default target)
# 		build the executable and all dependencies thereof
#
# 	dist
# 		create ssfi.tgz, a gzip'd tar file of the complete solution
#
# 	clean
# 		remove all intermediate objects
#
# 	clobber
# 		remove all intermediate and final files
#

PROG=	hashAndCompress

OBJS=	HashAndCompress.o

SRCS=	$(OBJS:%.o=%.cpp) HashAndCompress.h

EXTRAS=	Makefile

TARFILE= $(PROG:%=%.tgz)

CPPFLAGS += -std=c++11

# Boost libraries
LDLIBS= -lboost_system -lboost_thread -lboost_program_options -lboost_timer

# compression libraries
CPPFLAGS += -I../compression_perf/include
LDLIBS += -L ../compression_perf/lib -llz4 -llzf

# Skein hashing library
CPPFLAGS += -I../hashing_perf
LDLIBS += -L../hashing_perf/skein -l skein

LDLIBS += -lssl -lcrypto -lisal_crypto -ltbb

$(PROG): $(OBJS)
	$(LINK.cpp) $^ $(LDLIBS) -o $@

clean:
	/bin/rm -f $(OBJS)

clobber: clean
	/bin/rm -f $(PROG) $(TARFILE)

dist: $(TARFILE)

$(TARFILE): $(EXTRAS) $(SRCS)
	/usr/bin/tar cvfz $@ $^
