LIBFLAGS=-L/opt/vc/lib -lEGL -lGLESv2 -lbcm_host -lpthread -ljpeg -lshapes
CXXFLAGS=-DDEBUG -fno-exceptions -DPI -std=gnu++11 -I/opt/vc/include -I/opt/vc/include/interface/vmcs_host/linux -I/opt/vc/include/interface/vcos/pthreads -I..

objs=tcCalib.o

all: tcCalib

clean: $(objs)
	rm $(objs)
	rm tcCalib

tcCalib: $(objs)
	g++ -Wall $(CXXFLAGS) $(LIBFLAGS) $(objs) -o tcCalib

