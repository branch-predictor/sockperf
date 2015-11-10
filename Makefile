
LIBS=-lrt

CXXFLAGS=-I. -Wall -O2 -pipe -march=core2 -pthread $(LIBS)

.SUFFIXES:
.SUFFIXES: .cc .o .deps
all: spsrv spcli

SP_SRV_O=sockperf_srv.o
SP_CLI_O=sockperf_cli.o
SP_SRV_BIN=spsrv
SP_CLI_BIN=spcli

spsrv:	$(SP_SRV_O)
	$(CXX) -o $(SP_SRV_BIN) $(SP_SRV_O) -pthread $(LIBS)
spcli:	$(SP_CLI_O)
	$(CXX) -o $(SP_CLI_BIN) $(SP_CLI_O) $(LIBS)

clean:
	rm -f $(SP_SRV_BIN) $(SP_SRV_O) $(SP_CLI_BIN) $(SP_CLI_O)
