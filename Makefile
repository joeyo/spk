# install dependencies with make deps

TARGET = /usr/local/bin

all:
	tup

clean:
	rm -rf bin/* proto/*.pb.cc proto/*.pb.h proto/*.o src/*.o lib/*.o

ifeq ($(shell lsb_release -sc), stretch)
# as of April 2016.
DEPS = libgtk2.0-dev libgtk2.0-0-dbg \
	libgtkgl2.0-dev libgtkglext1-dev freeglut3-dev nvidia-cg-toolkit \
	libblas-common libblas-dev libopenblas-base libopenblas-dev \
	libarpack2 libarpack2-dev \
	libjack-jackd2-dev python-matplotlib \
	python-jsonpickle python-opengl libboost1.58-all-dev pkg-config \
	libhdf5-dev libsdl1.2-dev \
	libprotobuf-dev protobuf-compiler \
	astyle cppcheck \
	libprocps4 libprocps-dev \
	luajit libuuid1 uuid-dev libuuid1 \
	libzmq5 libzmq5-dbg libzmq3-dev \
	libgoogle-perftools-dev libgoogle-perftools4 libgoogle-perftools4-dbg \
	google-perftools \
	libcomedi0 libcomedi-dev \
	libxdg-basedir-dev libxdg-basedir1 libxdg-basedir1-dbg
else
DEPS = libgtk2.0-dev libgtk2.0-0-dbg \
	libgtkgl2.0-dev libgtkglext1-dev freeglut3-dev nvidia-cg-toolkit \
	libblas-common libblas-dev libopenblas-base libopenblas-dev \
	libarpack2 libarpack2-dev \
	libjack-jackd2-dev python-matplotlib \
	python-jsonpickle python-opengl libboost-dev pkg-config \
	libhdf5-dev libsdl1.2-dev \
	libprotobuf-dev protobuf-compiler \
	astyle cppcheck \
	libprocps3 libprocps3-dev \
	luajit libgl1-mesa-glx uuid-dev libuuid1 \
	libzmq3 libzmq3-dbg libzmq3-dev \
	libgoogle-perftools-dev libgoogle-perftools4 libgoogle-perftools4-dbg \
	google-perftools \
	libcomedi0 libcomedi-dev \
	libxdg-basedir-dev libxdg-basedir1 libxdg-basedir1-dbg
endif
deps:
	sudo apt-get install $(DEPS);
	@echo ""
	@echo "make sure non-free is in /etc/apt/sources.list for nvidia-cg-toolkit."
	@echo "also please install libarmadillo >= 6.7"

check:
	cppcheck -Iinclude -I/usr/local/include -I../lib --enable=all \
		-q src/*.cpp

pretty:
	# "-rm" means that make ignores errors, if any
	astyle -A8 --indent=tab -H -k3 include/*.h
	-rm include/*.h.orig
	astyle -A8 --indent=tab -H -k3 src/*.cpp
	-rm src/*.cpp.orig
	astyle -A8 --indent=tab -H -k3 lib/*.h
	-rm ../common_host/*.h.orig
	astyle -A8 --indent=tab -H -k3 lib/*.cpp
	-rm ../common_host/*.cpp.orig

install:
	install -d $(TARGET)
	install bin/spk -t $(TARGET)
	install bin/po8e -t $(TARGET)
	install bin/bp -t $(TARGET)
	install bin/lfp -t $(TARGET)
	install bin/notch -t $(TARGET)
	install bin/noop -t $(TARGET)
	install bin/af -t $(TARGET)
	install bin/af2 -t $(TARGET)
	install bin/latency_test -t $(TARGET)
	install bin/subtr -t $(TARGET)
	install bin/bbview -t $(TARGET)
	install bin/h5bbsave -t $(TARGET)
	install bin/h5evsave -t $(TARGET)
	install -d $(TARGET)/cg
	install cg/fade.cg -t $(TARGET)/cg
	install cg/fadeColor.cg -t $(TARGET)/cg
	install cg/threshold.cg -t $(TARGET)/cg

.PRECIOUS: proto/%.pb.cc proto/%.pb.h
