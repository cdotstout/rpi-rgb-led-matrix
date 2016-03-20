CXXFLAGS=-Wall -O3 -g 
#BINARIES=led-matrix minimal-example text-example rgbmatrix.so
BINARIES=partypole led-matrix minimal-example text-example

# Where our library resides. It is split between includes and the binary
# library in lib
RGB_INCDIR=include
RGB_LIBDIR=lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

GIFLIB_DIR=giflib-5.1.3/lib
GIFLIB_OBJS=$(GIFLIB_DIR)/dgif_lib.o \
	$(GIFLIB_DIR)/gifalloc.o \
	$(GIFLIB_DIR)/openbsd-reallocarray.o \

LDFLAGS+=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

all : $(BINARIES)

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

partypole : partypole.o $(GIFLIB_OBJS) $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) -o $@ partypole.o $(GIFLIB_OBJS) $(LDFLAGS)

led-matrix : demo-main.o $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) demo-main.o -o $@ $(LDFLAGS)

minimal-example : minimal-example.o $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) minimal-example.o -o $@ $(LDFLAGS)

text-example : text-example.o $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) text-example.o -o $@ $(LDFLAGS)

# Python module
rgbmatrix.so: rgbmatrix.o $(RGB_LIBRARY)
	$(CXX) -s -shared -lstdc++ -Wl,-soname,librgbmatrix.so -o $@ $< $(LDFLAGS)

%.o : %.cc
	$(CXX) -I$(RGB_INCDIR) -I$(GIFLIB_DIR) $(CXXFLAGS) -DADAFRUIT_RGBMATRIX_HAT -c -o $@ $<

clean:
	rm -f *.o $(OBJECTS) $(BINARIES)
	$(MAKE) -C lib clean
