
all: echo_client echo_server perf_client perf_server robust_test_client robust_test_server


CFLAGS = -g -O0

# appen --wrap [function] to wrap a function

LDFLAGS += -Wl,--wrap=socket -Wl,--wrap=bind -Wl,--wrap=listen -Wl,--wrap=connect -Wl,--wrap=accept -Wl,--wrap=read -Wl,--wrap=write -Wl,--wrap=close -Wl,--wrap=getaddrinfo -Wl,--wrap=setsockopt 

# compile with ../build/src/libPnx.a

LIBS += ../build/src/libPnx.a -lpcap -lpthread

%: %.c ../build/src/libPnx.a
	g++ -o $@ $^ unp.c $(LIBS) $(CFLAGS) $(LDFLAGS) 


robust_test_client: robust_test_client.cpp ../build/src/libPnx.a
	g++ -o $@ $^ $(LIBS) $(CFLAGS) $(LDFLAGS) 
	
robust_test_server: robust_test_server.cpp ../build/src/libPnx.a
	g++ -o $@ $^ $(LIBS) $(CFLAGS) $(LDFLAGS) 

clean:
	rm -f echo_client echo_server perf_client perf_server robust_test_client robust_test_server