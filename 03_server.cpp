#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <errno.h>

const size_t k_max_msg = 4096;

static void msg(const char *msg){
  fprintf(stderr,"%\n", msg);
}

static void die(const char *msg){
  int err = errno;
  fprintf(stderr, "[%d] %s\n",err,msg);
  abort();
}

//This function is used to set the file descriptor into non blocking mode
static void fd_set_nb(int fd){
  int errno=0;
  int flags = fcntl(fd, F_GETFL,0); // fcntl() function which is used for control over the files is used to get the flags of current file
  if(errno){
    die("fcntl error");
    retrun;
  }

  flags |= O_NONBLOCK;   //This is used to set the file descriptor to nonblocking mode, this means the IO operations would return immediatly even if they
                         // would block by default
  errno = 0;
  (void)fcntl(fd,F_SETFL,flags); // cast for ignoring the return value of fcntl
  if(errno){
    die("fcntl errno");
  }
}

static void conn_put(std::vector<Conn *> &f2conn,struct Conn *conn){
  if(f2conn.size() <= (size_t)conn->fd){
    f2conn.resize(conn->fd+1);
  }
  f2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn,int fd){
  //accept 
  struct sockaddr_in client_addr={};
  socklen_t socklen= sizeof(client_addr);

  int connfd = accept(fd, (struct sockaddr *)&client_addr,&socklen);
  if(connfd<0){
    msg("accept() error");
    return -1;
  }

  fd_set_nb(connfd);

  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if(!conn){
    close(connfd);
    return -1;
  }
  conn->fd=connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size=0;
  conn->wbuf_size=0;
  conn->wbuf_sent=0;
  conn_put(f2conn,conn);
  return 0;
}

static void state_req(Conn *conn);
static void state_res(Conn *conn);

static bool try_one_request(Conn *conn){

  if(conn->rbuf_size < 4){

    return false;

  }
  uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one request, do something with it
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // generating echoing response
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn){
  while(try_fill_buffer(conn)) {}
}

static void try_fill_buffer(Conn *conn){
  asset(conn->rbuf_size<sizeof(conn->rbuf));
  ssize_t rv=0;
  do{
    size_t cap = sizeof(conn->rbuf)-conn_<rbuf_size;
    rv=read(conn->fd, &conn->rbuf[conn->rbuf_size],cap);
  }while(rv<0 && errno == EINTR);

  if(rv<0 && errno==EAGAIN){
    return false;
  }
  if(rv<0){
    msg("read() error");
    conn->state = STATE_END;
    return false;
  }
  if(rv == 0){
    if(conn->rbuf_size>0){
      msg("unexpected EOF");
    }else{
      msg("EOF");
    }
    conn->state=STATE_END;
    return false;
  }
  conn->rbuf_size +=(size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf) - conn->rbuf_size);

  while(try_one_request(conn)){}
  return (conn->state==STATE_REQ);

}

static bool try_flush_buffer(Conn *conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        // response was fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}
static void state_res(Conn *conn){
  whiler(try_flush_buffer(conn)){}
}

static void connection_io(Conn *conn){
  if(conn->state==STATE_REQ) state_req(conn);
  else if(conn->state==STATE_RES) state_res(conn);
  else asser(0);
}

enum{
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,
};

struct Conn{
  int fd=-1;
  uint32_t=0; //for STATE_REQ or STATE_RES
  size_t rbuf_size= 0; //buffer for reading
  uint8_t rbuf[4];
  size_t wbuf_size=0; //buffer for writing
  size_t wbuf_sent=0;
  uint8_t wbuf[4+k_max_msg];
};

int main(){
  int fd=socket(AF_INET,SOCK_STREAM, 0); //AF_INET indicates tha this is an IPv4 socket and SOCK_STREAM indicates that this is stream oriented socket

  if(fd<0){
    die("socket()");
  }

  //map for client connections
  std::vector<Conn *> fd2conn;

  std::vector<struct pollfd> poll_args;
  while(true){
    poll_args.clear();
    struct pollfd pfd = {fd,POLLIN,0};
    poll_args.push_back(pfd);

    for(Conn *conn:fd2conn){
      if(!conn){
        continue;
      }

      struct pollfd pfd = {};
      pfd.fd=conn->fd;
      pfd.events=(conn->state==STATE_REQ) ? POLLIN :POLLOUT;
      pfd.events=pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }


    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if(rv<0){
      die("poll");
    }

    for(size_t i =1; i< poll_args.size();++i){
      if(poll_args[i].revents){
        Conn *conn = fd2conn[poll_args[i].fd];
        connection.io(conn);
        if(conn->state==STATE_END){
          //client closed this connection or something bad happened close this connection
          fd2conn[conn->fd]=NULL;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }

    //try accepting new connection if the listening fd is active
    if(poll_args[0].revents){
      (void)accept_new_conn(f2conn,fd);
    }
  }
  return 0;
}
