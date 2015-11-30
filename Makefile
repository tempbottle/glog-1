
CPPFLAGS += -isystem $(GTEST_DIR)/include -std=c++14 -stdlib=libc++
CXXFLAGS += -g -Wall -Wextra


GITHUB_PATH=/Users/dengoswei/project/src/github.com/

INCLS += -I $(GITHUB_PATH)/dengoswei/cpaxos -I $(GITHUB_PATH)/dengoswei/cpaxos/cpaxospb/
INCLS += -I./utils/ -I./glogpb/
INCLS += -I/Users/dengoswei/open-src/github.com/tanakh/cmdline/
INCLS += -I/Users/dengoswei/open-src/github.com/microsoft/GSL/include
INCLS += -I/Users/dengoswei/project/include
INCLS += -I/usr/local/include
LINKS += -L/Users/dengoswei/project/lib
LINKS += -lpthread -lprotobuf -lcpaxos
LINKS += -lgrpc++_unsecure -lgrpc -lgpr -ldl 

CPPCOMPILE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(INCLS) -c -o $@
BUILDEXE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LINKS)

PROTOS_PATH = glogpb/:$(GITHUB_PATH)/dengoswei/cpaxos/cpaxospb/
PROTOC = /Users/dengoswei/project/bin/protoc
GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`

all: $(TESTS)

clean :
	rm -f glog_server glog_tools *.o ./glogpb/*.o ./utils/*.o  glogpb/*.pb.cc glogpb/*.pb.h

glog_server: glog_server.o glog_server_impl.o glog_client_impl.o \
	glogpb/glog.grpc.pb.o glogpb/glog.pb.o utils/config.o
	$(BUILDEXE)

glog_tools: glog_tools.o glog_client_impl.o glogpb/glog.grpc.pb.o glogpb/glog.pb.o
	$(BUILDEXE)

%.grpc.pb.cc: glogpb/%.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=glogpb/ --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

%.pb.cc: glogpb/%.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=glogpb/ $<

%.o:%.cc
	$(CPPCOMPILE)

#.cc.o:
#	$(CPPCOMPILE)

