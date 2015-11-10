#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#include <vector>
#include <netinet/in.h>
#include <signal.h>

using namespace std;

int unix_sock;
int tcp_sock;

struct bench_sock {
  int fd;
  pthread_t fd_t;
};

pthread_t unix_t;
pthread_t tcp_t;
vector<bench_sock*> sockets;

void show_err(const char* msg) {
  int err = errno;
  printf("%s: %s (%d)\n", msg, strerror(err), err);
}

// "I'll send you some data"
const char BENCH_READ_DATA = 0x01;

// "Send me some data"
const char BENCH_WRITE_DATA = 0x02;

// "PLZDIE, KTHXBAI"
const char BENCH_QUIT = 0x03;

const size_t BENCH_TEMPL_SIZE = 65536;

// random data
char* bench_template = NULL;

unsigned long long gettickcount(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (ts.tv_nsec) + ts.tv_sec * 1000000000;
}

int read_all(int fd, void* dest, size_t size) {
  size_t done = 0;

  while (done < size) {
    size_t rd = read(fd, (void*)(((char*)dest)+done), size-done);
    if (rd > 0) {
      done += rd;
    } else {
      break;
    }
  }

  return done;
}

int write_all(int fd, void* dest, size_t size) {
  size_t done = 0;

  while (done < size) {
    size_t wd = write(fd, (void*)(((char*)dest)+done), size-done);
    if (wd > 0) {
      done += wd;
    } else {
      break;
    }
  }

  return done;
}

int bench_read(int fd, uint64_t block_size, size_t stride) {

  if (bench_template == NULL) {
    bench_template = (char*)malloc(BENCH_TEMPL_SIZE);
    if (!bench_template) {
      return 1;
    }
    srand48(time(NULL));
    int* bufout = (int*) bench_template;
    for (size_t i = 0; i < BENCH_TEMPL_SIZE / sizeof(int); i++) {
      *bufout = lrand48();
      bufout++;
    }
  }

  char* buffer = (char*) malloc(stride);

  if (stride > BENCH_TEMPL_SIZE) {
    for (size_t i = 0; i < stride / BENCH_TEMPL_SIZE; i++) {
      memcpy((void*)(&buffer[i * BENCH_TEMPL_SIZE]), bench_template, BENCH_TEMPL_SIZE);
    }
  } else {
    memcpy((void*)buffer, bench_template, stride);
  }

  buffer[0] = BENCH_READ_DATA;
  *((uint64_t*) &buffer[1]) = block_size;
  *((int*) &buffer[1+sizeof(uint64_t)]) = stride;

  if (write_all(fd, buffer, sizeof(block_size)+sizeof(char)+sizeof(int)) ==
        (sizeof(block_size)+sizeof(char)+sizeof(int))) {

    if (stride > BENCH_TEMPL_SIZE) {
      for (size_t i=0; i < stride / BENCH_TEMPL_SIZE; i++) {
        memcpy((void*)(&buffer[i * BENCH_TEMPL_SIZE]), bench_template, BENCH_TEMPL_SIZE);
      }
    } else {
      memcpy((void*)buffer, bench_template, stride);
    }

    uint64_t data_left = block_size;

    while (data_left > 0) {
      size_t send_size = data_left < stride ? data_left : stride;
      send_size = write_all(fd, buffer, send_size);
      if (send_size > 0) {
        data_left -= send_size;
      } else {
        free(buffer);
        return 1;
      }
    }
  }

  free(buffer);
  return 0;

}

int bench_write(int fd, uint64_t block_size, size_t stride) {

  char* buffer = (char*) malloc(stride);
  buffer[0] = BENCH_WRITE_DATA;
  *((uint64_t*) &buffer[1]) = block_size;
  *((int*) &buffer[1+sizeof(uint64_t)]) = stride;

  if (write_all(fd, buffer, sizeof(block_size)+sizeof(char)+sizeof(int))
      == (sizeof(block_size)+sizeof(char)+sizeof(int))) {
    uint64_t data_left = block_size;

    while (data_left > 0) {
      int read_size = data_left < stride ? data_left : stride;
      read_size = read_all(fd, buffer, read_size);
      if (read_size > 0) {
        data_left -= read_size;
      } else {
        perror("bench_write");
        free(buffer);
        return 1;
      }
    }
  }
  free(buffer);

  return 0;

}

int bench_write_batch(int fd, uint64_t block_size, int stride_size) {
  unsigned long long ti;
  int r = 0;

  for (int i=0; i < 3; i++) {
    ti = gettickcount();
    r = bench_write(fd, 1048576l*10240l, stride_size);
    ti = gettickcount()-ti;
    printf("Wrote 10G using %dk stride in %.2fs (%.2f MB/s)\n", stride_size/1024, ti/1000000000.0, (10240l)/(ti/1000000000.0));
    if (r)
        return r;
  }
  return 0;
}

int bench_read_batch(int fd, uint64_t block_size, int stride_size) {
  unsigned long long ti;
  int r = 0;

  for (int i=0; i < 3; i++) {
    ti = gettickcount();
    r = bench_read(fd, 1048576l*10240l, stride_size);
    ti = gettickcount()-ti;
    printf("Read 10G using %dk stride in %.2fs (%.2f MB/s)\n", stride_size/1024, ti/1000000000.0, (10240l)/(ti/1000000000.0));
    if (r)
        return r;
  }
  return 0;
}

const uint64_t TEST_CHUNK_SIZE = 1048576l*1024l;

void perform_bench(int fd) {

  printf("Write test...\n");
  bench_write_batch(fd, TEST_CHUNK_SIZE, 1024);
  bench_write_batch(fd, TEST_CHUNK_SIZE, 2048);
  bench_write_batch(fd, TEST_CHUNK_SIZE, 4096);
  bench_write_batch(fd, TEST_CHUNK_SIZE, 65536);
  bench_write_batch(fd, TEST_CHUNK_SIZE, 262144);
  bench_write_batch(fd, TEST_CHUNK_SIZE, 1048576);

  printf("Read test...\n");
  bench_read_batch(fd, TEST_CHUNK_SIZE, 1024);
  bench_read_batch(fd, TEST_CHUNK_SIZE, 2048);
  bench_read_batch(fd, TEST_CHUNK_SIZE, 4096);
  bench_read_batch(fd, TEST_CHUNK_SIZE, 65536);
  bench_read_batch(fd, TEST_CHUNK_SIZE, 262144);
  bench_read_batch(fd, TEST_CHUNK_SIZE, 1048576);

  char cmd = BENCH_QUIT;

  write_all(fd, &cmd, sizeof(char));
}

const char* addr = "/tmp/testsock.sck" ;

void test_tcp(void) {
  printf("testing tcp socket...\n");

  tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (tcp_sock == -1) {
    show_err("Failed to make unix_sock");
    return;
  }

  sockaddr_in srcaddr;
  memset(&srcaddr, 0, sizeof(srcaddr));
  srcaddr.sin_port = htons(64666);
  srcaddr.sin_family = AF_INET;
  srcaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(tcp_sock, (sockaddr*)&srcaddr, sizeof(srcaddr)) == 0) {
    perform_bench(tcp_sock);
  } else {
    show_err("Failed to connect to TCP server");
  }
  close(tcp_sock);
}

void test_unix(void) {
  printf("Testing unix socket...\n");

  unix_sock = socket(AF_LOCAL, SOCK_STREAM, 0);
  if (unix_sock == -1) {
    show_err("Failed to make unix_sock");
    return;
  }

  sockaddr_un srcaddr;
  memset(&srcaddr, 0, sizeof(srcaddr));
  srcaddr.sun_family = AF_UNIX;
  strcpy(srcaddr.sun_path, addr);

  if (connect(unix_sock, (sockaddr*)&srcaddr, sizeof(srcaddr)) == 0) {
    perform_bench(unix_sock);
  } else {
    show_err("Failed to connect to UNIX server");
  }
  close(unix_sock);

}

int main(int argc, char** argv) {

  signal(SIGPIPE, SIG_IGN);
  test_tcp();
  test_unix();
  printf("Exiting...\n");

  return 0;
}