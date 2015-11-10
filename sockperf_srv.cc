#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
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

// Unix socket file
const char* addr = "/tmp/testsock.sck" ;

// "I'll send you some data"
const char BENCH_READ_DATA = 0x01;

// "Send me some data"
const char BENCH_WRITE_DATA = 0x02;

// "PLZDIE, KTHXBAI"
const char BENCH_QUIT = 0x03;

const size_t BENCH_TEMPL_SIZE = 65536;

// random data buffer so we don't waste time for rnd data generation
// during benchmark
char* bench_template = NULL;

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

int bench_write(int fd) {

  if (bench_template == NULL) {
    bench_template = (char*)malloc(BENCH_TEMPL_SIZE);
    if (!bench_template) {
      return 1;
    }
    srand48(time(NULL));
    int* bufout = (int*) bench_template;
    for (size_t i=0; i < BENCH_TEMPL_SIZE / sizeof(int); i++) {
      *bufout = lrand48();
      bufout++;
    }
  }

  uint64_t block_size = 0;

  if (read_all(fd, &block_size, sizeof(block_size)) == sizeof(block_size)) {

    uint64_t data_left = block_size;
    size_t stride = 1024;
    if (read_all(fd, &stride, sizeof(int)) != sizeof(int)) {
      return 0;
    }
    if ((stride <= 0) || (stride > 256*1048576)) {
      return 0;
    }
    char* buffer = (char*) malloc(stride);

    if (stride > BENCH_TEMPL_SIZE) {
      for (size_t i=0; i < stride / BENCH_TEMPL_SIZE; i++) {
        memcpy((void*)(&buffer[i * BENCH_TEMPL_SIZE]), bench_template, BENCH_TEMPL_SIZE);
      }
    } else {
      memcpy((void*)buffer, bench_template, stride);
    }

    while (data_left > 0) {
      size_t send_size = data_left < stride ? data_left : stride;
      send_size = write_all(fd, &buffer[0], send_size);
      if (send_size > 0) {
        data_left -= send_size;
      } else {
        free(buffer);
        return 1;
      }
    }
    free(buffer);
  }

  return 0;

}

int bench_read(int fd) {

  uint64_t block_size = 0;

  if (read_all(fd, &block_size, sizeof(block_size)) == sizeof(block_size)) {
    uint64_t data_left = block_size;
    size_t stride = 1024;
    if (read_all(fd, &stride, sizeof(int)) != sizeof(int)) {
      return 0;
    }
    if ((stride <= 0) || (stride > 256*1048576)) {
      return 0;
    }
    char* buffer = (char*) malloc(stride);

    while (data_left > 0) {
      size_t read_size = data_left < stride ? data_left : stride;
      read_size = read_all(fd, buffer, read_size);
      if (read_size > 0) {
        data_left -= read_size;
      } else {
        return 1;
      }
    }
    free(buffer);
  }

  return 0;

}

int parse_commands(int fd) {

  while (1) {
    char cmd = 0;
    if (read(fd, &cmd, 1) == 1) {
      switch (cmd) {
        case BENCH_READ_DATA:
          if (bench_read(fd)) {
            return 0;
          }
          break;
        case BENCH_WRITE_DATA:
          if (bench_write(fd)) {
            return 0;
          }
          break;
        case BENCH_QUIT:
        default:
          return 0;
      }
    } else {
      return 0;
    }
  }
}

static void* sock_loop(void* arg) {

  bench_sock* bs = (bench_sock*)arg;

  while (1) {
    fd_set rfd;
    fd_set efd;
    FD_ZERO(&rfd);
    FD_SET(bs->fd, &rfd);
    FD_ZERO(&efd);
    FD_SET(bs->fd, &efd);
    int r = select(bs->fd+1, &rfd, NULL, &efd, NULL);
    if (r == -1) {
      close(bs->fd);
      break;
    }
    if (FD_ISSET(bs->fd, &rfd)) {
      if (parse_commands(bs->fd) == 0) {
        close(bs->fd);
        break;
      }
    } else if (FD_ISSET(bs->fd, &efd)) {
      close(bs->fd);
      break;
    }
  }

  return NULL;

}

static void* server_loop(void* arg) {

  int fd = *((int*)arg);
  printf("Socket listener on socket %d\n", fd);
  while (1) {
    fd_set rfd;
    fd_set efd;
    FD_ZERO(&rfd);
    FD_SET(fd, &rfd);
    FD_ZERO(&efd);
    FD_SET(fd, &efd);
    int r = select(fd+1, &rfd, NULL, &efd, NULL);
    if (r == -1) {
      close(fd);
      printf("r == -1\n");
      break;
    }
    if (FD_ISSET(fd, &rfd)) {
      bench_sock* newsock = new bench_sock();
      newsock->fd = accept(fd, NULL, NULL);
      if (newsock->fd >= 0) {
        if (pthread_create(&newsock->fd_t, NULL, sock_loop, (void*)newsock)) {
          show_err("failed to make thread");
          close(newsock->fd);
          delete(newsock);
          continue;
        }
        sockets.push_back(newsock);;
      }

    } else if (FD_ISSET(fd, &efd)) {
      close(fd);
      break;
    }
  }
  return NULL;

}

void start_tcp(void) {

  tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (tcp_sock == -1) {
    show_err("Failed to make tcp_sock");
    return;
  }

  sockaddr_in srcaddr;
  memset(&srcaddr, 0, sizeof(sockaddr_in));
  srcaddr.sin_port = htons(64666);
  srcaddr.sin_addr.s_addr = INADDR_ANY;
  srcaddr.sin_family = AF_INET;

  int optval = 1;
  setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  if (bind(tcp_sock, (sockaddr*) &srcaddr, sizeof(srcaddr))) {
    show_err("Failed to bind tcp_sock");
    close(unix_sock);
    return;
  }
  if (listen(tcp_sock, SOMAXCONN)) {
    show_err("Failed to set tcp socket as listening");
    close(unix_sock);
    return;
  }

  if (pthread_create(&tcp_t, NULL, server_loop, (void*)(&tcp_sock))) {
    show_err("Failed to start tcp socket thread");
    close(tcp_sock);
  }

  printf("TCP started on fd %d\n", tcp_sock);

}

void start_unix(void) {

  unix_sock = socket(AF_LOCAL, SOCK_STREAM, 0);
  if (unix_sock == -1) {
    show_err("Failed to make unix_sock");
    return;
  }

  sockaddr_un srcaddr;
  memset(&srcaddr, 0, sizeof(srcaddr));
  srcaddr.sun_family = AF_UNIX;
  strcpy(srcaddr.sun_path, addr);

  int optval = 1;
  setsockopt(unix_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  unlink(addr);

  if (bind(unix_sock, (sockaddr*) &srcaddr, sizeof(srcaddr))) {
    show_err("Failed to bind unix_sock");
    close(unix_sock);
    return;
  }
  if (listen(unix_sock, SOMAXCONN)) {
    show_err("Failed to set unix socket as listening");
    close(unix_sock);
    return;
  }

  if (pthread_create(&unix_t, NULL, server_loop, (void*)(&unix_sock))) {
    show_err("Failed to start unix socket thread");
    close(unix_sock);
  }

  printf("UNIX started on fd %d\n", unix_sock);

}

int main(int argc, char** argv) {

  signal(SIGPIPE, SIG_IGN);
  start_tcp();
  start_unix();
  pthread_join(unix_t, NULL);
  pthread_join(tcp_t, NULL);
  printf("Exiting...\n");

  for (vector<bench_sock*>::iterator iter = sockets.begin(); iter != sockets.end(); iter++) {
    bench_sock* s = (bench_sock*)(*iter);
    pthread_join(s->fd_t, NULL);
    delete(s);
  }

  return 0;
}