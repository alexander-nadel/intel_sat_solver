##
##  Template makefile for Standard, Profile, Debug, Release, and Release-static versions
##
##    eg: "make rs" for a statically linked release version.
##        "make d"  for a debug version (no optimizations).
##        "make"    for the standard version (optimized, but with debug information and assertions active)

PWD        = $(shell pwd)
EXEC      ?= $(notdir $(PWD))
LIB       ?= $(notdir $(PWD))

CSRCS      = $(wildcard *.cc) 
DSRCS      = $(foreach dir, $(DEPDIR), $(filter-out $(MROOT)/$(dir)/Main.cc, $(wildcard $(MROOT)/$(dir)/%.cc)))
CHDRS      = $(wildcard *.h)
COBJS      = $(CSRCS:.cc=.o) $(DSRCS:.cc=.o)

PCOBJS     = $(addsuffix p,  $(COBJS))
DCOBJS     = $(addsuffix d,  $(COBJS))
RCOBJS     = $(addsuffix r,  $(COBJS))
HCOBJS     = $(addsuffix h,  $(COBJS))
QCOBJS     = $(addsuffix q,  $(COBJS))

CXX       ?= g++

CFLAGS    ?= -DSKIP_ZLIB -Wall -Wno-parentheses -std=c++20
LFLAGS    ?= -Wall -lpthread 

COPTIMIZE ?= -O3

.PHONY : s p d r rs rh clean 

s:	$(EXEC)
p:	$(EXEC)_profile
d:	$(EXEC)_debug
r:	$(EXEC)_release
rs:	$(EXEC)_static
rh:	$(EXEC)_shared_release
dh:	$(EXEC)_shared_debug

libs:	lib$(LIB)_standard.a
libp:	lib$(LIB)_profile.a
libd:	lib$(LIB)_debug.a
libr:	lib$(LIB)_release.a
librh:	lib$(LIB)_shared_release.so
libdh:	lib$(LIB)_shared_debug.so

## Compile options
%.o:			CFLAGS +=$(COPTIMIZE) -g
%.op:			CFLAGS +=$(COPTIMIZE) -pg -g -D NDEBUG
%.od:			CFLAGS +=-g -gdwarf-2
%.or:			CFLAGS +=$(COPTIMIZE) -D NDEBUG
%.oh:			CFLAGS +=$(COPTIMIZE) -D NDEBUG -fPIC
%.oq:			CFLAGS +=-g -gdwarf-2 -fPIC

## Link options
$(EXEC):		LFLAGS += -g
$(EXEC)_profile:	LFLAGS += -g -pg
$(EXEC)_debug:		LFLAGS += -g
#$(EXEC)_release:	LFLAGS += ...
$(EXEC)_static:		LFLAGS += --static
$(EXEC)_shared_release:		LFLAGS += -L. -l$(LIB)_shared_release -Wl,-rpath,$${ORIGIN}
$(EXEC)_shared_debug:		LFLAGS += -L. -l$(LIB)_shared_debug -Wl,-rpath,$${ORIGIN}
lib$(LIB)_shared_release.so:	LFLAGS += -fPIC -shared
lib$(LIB)_shared_debug.so:	LFLAGS += -fPIC -shared

## Dependencies
$(EXEC):		$(COBJS)
$(EXEC)_profile:	$(PCOBJS)
$(EXEC)_debug:		$(DCOBJS)
$(EXEC)_release:	$(RCOBJS)
$(EXEC)_static:		$(RCOBJS)
$(EXEC)_shared_release:		lib$(LIB)_shared_release.so Main.oh
$(EXEC)_shared_debug:		lib$(LIB)_shared_debug.so Main.oq

lib$(LIB)_standard.a:	$(filter-out %/Main.o,  $(COBJS))
lib$(LIB)_profile.a:	$(filter-out %/Main.op, $(PCOBJS))
lib$(LIB)_debug.a:	$(filter-out %/Main.od, $(DCOBJS))
lib$(LIB)_release.a:	$(filter-out %/Main.or, $(RCOBJS))
lib$(LIB)_shared_release.so:	$(filter-out %/Main.oh, $(HCOBJS))
lib$(LIB)_shared_debug.so:	$(filter-out %/Main.oq, $(QCOBJS))

## Build rule
%.o %.op %.od %.or %.oh %.oq:	%.cc
	@echo Compiling: $@
	@$(CXX) $(CFLAGS) -c -o $@ $<

## Linking rules (standard/profile/debug/release)
$(EXEC) $(EXEC)_profile $(EXEC)_debug $(EXEC)_release $(EXEC)_static $(EXEC)_shared_release $(EXEC)_shared_debug:
	@echo Linking: "$@ ( $(foreach f,$^,$f) )"
	@$(CXX) $^ $(LFLAGS) -o $@

## Library rules (standard/profile/debug/release)
lib$(LIB)_standard.a lib$(LIB)_profile.a lib$(LIB)_release.a lib$(LIB)_debug.a:
	@echo Making library: "$@ ( $(foreach f,$^,$f) )"
	@$(AR) -rcsv $@ $^

## Shared libraries
lib$(LIB)_shared_release.so lib$(LIB)_shared_debug.so:
	@$(CXX) $^ $(LFLAGS) -o $@

## Library Soft Link rule:
libs libp libd libr:
	@echo "Making Soft Link: $^ -> lib$(LIB).a"
	@ln -sf $^ lib$(LIB).a
librh libdh:
	@echo "Making Soft Link: $^ -> lib$(LIB).so"
	@ln -sf $^ lib$(LIB).so

## Clean rule
allclean: clean
	
	@rm -f ../simp/*.o ../simp/*.or ../simp/*.od  ../core/*.o ../core/*.or ../core/*.od ../core/*.oh  ../core/*.oq
clean:
	rm -f $(EXEC) $(EXEC)_profile $(EXEC)_debug $(EXEC)_release $(EXEC)_static $(EXEC)_shared_* lib$(LIB)_*.a lib$(LIB)_profile.a lib$(LIB)_debug.a lib$(LIB)_release.a lib$(LIB)*.so lib$(LIB).a\
	  $(COBJS) $(PCOBJS) $(DCOBJS) $(RCOBJS) $(HCOBJS) $(QCOBJS) *.core depend.mk 

## Make dependencies
depend.mk: $(CSRCS) $(CHDRS)
	@echo Making dependencies
	@$(CXX) $(CFLAGS) -I$(MROOT) \
	   $(CSRCS) -MM | sed 's|\(.*\):|$(PWD)/\1 $(PWD)/\1r $(PWD)/\1d $(PWD)/\1p:|' > depend.mk
	@for dir in $(DEPDIR); do \
	      if [ -r $(MROOT)/$${dir}/depend.mk ]; then \
		  echo Depends on: $${dir}; \
		  cat $(MROOT)/$${dir}/depend.mk >> depend.mk; \
	      fi; \
	  done

-include depend.mk
