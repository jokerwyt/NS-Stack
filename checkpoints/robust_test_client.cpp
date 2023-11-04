#include <iostream>
#include <string>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <vector>

using namespace std;

bool write_all(int fd, const char *buf, int len) {
  int n = 0;
  while (n < len) {
    int ret = write(fd, buf + n, len - n);
    if (ret < 0) {
      return false;
    }
    n += ret;
  }
  return true;
}

bool read_all(int fd, char *buf, int len) {
  int n = 0;
  while (n < len) {
    int ret = read(fd, buf + n, len - n);
    if (ret < 0) {
      return false;
    }
    n += ret;
  }
  return true;
}


int main(int argc, char *argv[]) {

  // get a socket and connect

  std::vector<std::thread> threads;


  // take the first arg as the number of threads
  int num_threads = atoi(argv[1]);

  for (int i = 0; i < num_threads; i++) {
    std::thread t([=]() {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);

        if (sockfd < 0) {
          std::cout << "socket error" << std::endl;
          return -1;
        }

        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;

        servaddr.sin_port = htons(12345);

        if (inet_pton(AF_INET, "10.100.1.1", &servaddr.sin_addr) <= 0) {
          std::cout << "inet_pton error" << std::endl;
          return -1;
        }

        if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
          std::cout << "connect error" << std::endl;
          return -1;
        }

        char buf[1024], buf2[1024];
        for (int i = 0; i < 10; i++) buf2[i] = buf[i] = rand();

        if (write_all(sockfd, buf, 10) == false) {
          std::cout << "write error" << std::endl;
          return -1;
        }

        if (read(sockfd, buf2, 10) == false) {
          std::cout << "read error" << std::endl;
          return -1;
        }

        // compare
        for (int i = 0; i < 10; i++) {
          if (buf[i] != buf2[i]) {
            std::cout << "compare error" << std::endl;
            return -1;
          }
        }

        close(sockfd);
        return 0;
      }
    );
    threads.push_back(std::move(t));
  }

  for (auto &t : threads) {
    t.join();
  }
  cout << "Finished" << endl;

  sleep(1); // wait connection close
}