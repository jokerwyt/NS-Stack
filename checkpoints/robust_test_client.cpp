#include <iostream>
#include <string>
#include <netinet/in.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <vector>

using namespace std;

int main(int argc, char *argv[]) {

  // get a socket and connect

  std::vector<std::thread> threads;
  for (int i = 0; i < 1; i++) {
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

        char buf[1024];
        memset(buf, 'a', sizeof(buf));
        int n = write(sockfd, buf, 1);
        if (n < 0) {
          std::cout << "write error" << std::endl;
          return -1;
        }
        buf[0] = 'b';
        n = read(sockfd, buf, 1);
        if (n < 0) {
          std::cout << "read error" << std::endl;
          return -1;
        }

        if (buf[0] != 'a') {
          std::cout << "echo back error" << std::endl;
          return -1;
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
}