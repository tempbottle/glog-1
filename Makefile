
CPPFLAGS += -isystem $(GTEST_DIR)/include -std=c++14 -stdlib=libc++
CXXFLAGS += -g -Wall -Wextra



TESTS = utils_test paxos_instance_test paxos_test paxos_pb_test config_test glog_test

INCLS += -I../basic -I../proto -I../glog
INCLS += -I/Users/dengoswei/open-src/github.com/microsoft/GSL/include
INCLS += -I/Users/dengoswei/project/include
LINKS += -L/Users/dengoswei/project/lib
LINKS += -lpthread -lprotobuf
LINKS += -lgrpc++_unsecure -lgrpc -lgpr -ldl

GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
                $(GTEST_DIR)/include/gtest/internal/*.h

CPPCOMPILE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(INCLS) -c -o $@
BUILDEXE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LINKS)

GITHUB_PATH=/Users/dengoswei/project/src/github.com/

PROTOS_PATH = glogpb/:$(GITHUB_PATH)/dengoswei/cpaxos/cpaxospb/
PROTOC = /Users/dengoswei/project/bin/protoc
GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`

all: $(TESTS)

clean :
	rm -f $(TESTS) gtest.a gtest_main.a *.o ../basic/*.o ../proto/*.o ../glog/*.o

# Builds gtest.a and gtest_main.a.

# Usually you shouldn't tweak such internal variables, indicated by a
# trailing _.
GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
gtest-all.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest-all.cc

gtest_main.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest_main.cc

gtest.a : gtest-all.o
	$(AR) $(ARFLAGS) $@ $^

gtest_main.a : gtest-all.o gtest_main.o
	$(AR) $(ARFLAGS) $@ $^


config_test: config_test.o ../basic/config.o gtest_main.a
	$(BUILDEXE)

glog_test: glog_test.o ../proto/glog.pb.o ../proto/glog.grpc.pb.o ../proto/paxos.pb.o \
	../glog/glog_server_impl.o ../glog/glog_client_impl.o ../basic/paxos.o ../basic/paxos_impl.o \
	../basic/paxos_instance.o ../basic/config.o
	$(BUILDEXE)

%.grpc.pb.cc: glogpb/%.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=glogpb/ --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

%.pb.cc: glogpb/%.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=glogpb/ $<

%.o:%.cc
	$(CPPCOMPILE)

#.cc.o:
#	$(CPPCOMPILE)

