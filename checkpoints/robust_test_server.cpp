#include <iostream>
#include <string>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("usage: %s <Port>\n", argv[0]);
    return -1;
  }

  // get the server port
  int port = atoi(argv[1]);


  // get a socket and listen
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    std::cout << "socket error" << std::endl;
    return -1;
  }

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;

  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(port);

  if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
    std::cout << "bind error" << std::endl;
    return -1;
  }

  if (listen(listenfd, 1024) < 0) {
    std::cout << "listen error" << std::endl;
    return -1;
  }

  // accept and echo
  int cnt = 0;
  while (1) {
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    int connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);
    cnt ++;
    if (connfd < 0) {
      std::cout << "accept error" << std::endl;
      return -1;
    }

    char buf[1024];
    int n = read(connfd, buf, 1024);
    if (n < 0) {
      std::cout << "read error" << std::endl;
      return -1;
    }

    write(connfd, buf, n);
    close(connfd);
    cout << "cnt: " << cnt << endl;
  }
}
