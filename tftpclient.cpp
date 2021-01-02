#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "common.h"
#include "package.h"

using namespace std;

bool isGet;
char* serv_addr_s;
uint16_t serv_port;
char* filename;
struct sockaddr_in make_addr(char* addr, int port) {
  struct sockaddr_in serv_addr;
  bzero((char*)&serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(addr);
  if (INADDR_NONE == serv_addr.sin_addr.s_addr) {
    puts("invaild address!");
    exit(1);
  }
  serv_addr.sin_port = htons(port);
  return serv_addr;
}
int host_UDPsocket(int port) {
  int sockfd;
  struct sockaddr_in host_addr;
  if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
    puts("create local socket failed");
    exit(1);
  }
  bzero((char*)&host_addr, sizeof(host_addr));
  host_addr.sin_family = AF_INET;
  host_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  host_addr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr*)&host_addr, sizeof(host_addr)) < 0) {
    puts("bind local socket failed");
    return -1;
  }
  return sockfd;
}

template <typename Derived>
Status send_package(int sockfd, struct sockaddr* sockaddr,
                    PackageBase<Derived>& package) {
  uint rand100 = rand() % 100u;
  if (rand100 < k_drop_percent) return Status::Ok;
  char* buffer;
  buffer = (char*)malloc(package.size());
  package.serialize(buffer);
  if (-1 == sendto(sockfd, buffer, package.size(), 0,
                   (struct sockaddr*)sockaddr, sizeof(struct sockaddr)))
    return Status::SendErr;
  return Status::Ok;
}
void recv_timeout_handler(int sig) { return; }
Err last_err_package;
template <typename Derived>
Status recv_package(int sockfd, struct sockaddr* sockaddr,
                    PackageBase<Derived>& package) {
  static char buffer[k_buffer_size];
  size_t len;
  size_t addr_len = sizeof(sockaddr);

  struct sigaction timeout_action;

  timeout_action.sa_handler = recv_timeout_handler;
  timeout_action.sa_flags = 0;
  sigaction(SIGALRM, &timeout_action, 0);
  struct itimerval tick;
  tick.it_value.tv_sec = k_timeout_us / 1000000;
  tick.it_value.tv_usec = k_timeout_us % 1000000;
  tick.it_interval.tv_sec = 0;
  tick.it_interval.tv_usec = 0;

  struct itimerval zero_it;
  memset(&zero_it, 0, sizeof(zero_it));

  setitimer(ITIMER_REAL, &tick, NULL);
  errno = 0;

  while (true) {
    len = recvfrom(sockfd, buffer, k_buffer_size, 0, sockaddr,
                   (socklen_t*)&addr_len);
    if (errno != 0) {
      if (errno == EINTR) {
        //("timeout");
      }
      setitimer(ITIMER_REAL, &zero_it, NULL);
      return Status::Timeout;
    }
    Status status = package.deserialize(buffer, len);
    if (Status::Ok == status) {
      return Status::Ok;
    } else if (Status::ServerErr == status) {
      last_err_package.deserialize(buffer, len);
      return Status::ServerErr;
    } else if (Status::PackageDeserializeErr == status) {
      continue;
    }
  }
  return Status::UnknownErr;
}

void get_file() {
  uint sum_size = 0;
  int sockfd;
  struct sockaddr_in serv_addr;
  FILE* file;
  if (strchr(filename, '/') != NULL) {
    printf(
        "do not support file transfer out of the current working directory\n");
    return;
  }
  file = fopen(filename, "wb");
  if (file == NULL) {
    perror(filename);
    return;
  }

  sockfd = host_UDPsocket(0);
  serv_addr = make_addr(serv_addr_s, serv_port);

  if (sockfd < 0) {
    printf("Couldn't open socket\n");
    return;
  }

  ReadRequest read_request;
  read_request.filename = std::string(filename);
  read_request.mode = std::string("octet");
  if (Status::Ok !=
      send_package(sockfd, (struct sockaddr*)&serv_addr, read_request)) {
    puts("send failed");
    exit(1);
  }
  uint timeout_cnt = 0;
  uint16_t block_num = 1;
  Ack ack;
  while (1) {
    Data data_package;
    Status status =
        recv_package(sockfd, (struct sockaddr*)&serv_addr, data_package);
    if (Status::Ok == status) {
      fprintf(stderr, "%d block get\n", block_num);
      timeout_cnt = 0;
      if (data_package.block_num == block_num) {
        sum_size+=data_package.data.size();
        if (data_package.data.size() == 0 ||
            fwrite(data_package.data.c_str(), 1, data_package.data.size(),
                   file)) {
        } else {
          perror(filename);
          goto ERROR_LABAL;
        }
        ack.block_num = block_num++;
        send_package(sockfd, (struct sockaddr*)&serv_addr, ack);
        if (data_package.data.size() < 512) {
          goto OK_LABAL;
        }
      } else {
        continue;
      }
    } else if (Status::Timeout == status) {
      fprintf(stderr, "%d block timeout\n", block_num);
      timeout_cnt++;
      if (timeout_cnt >= k_timeout_ms) {
        printf("There have been %d times timeout\n", timeout_cnt);
        goto ERROR_LABAL;
      } else {
        if (block_num == 1) {
          if (Status::Ok != send_package(sockfd, (struct sockaddr*)&serv_addr,
                                         read_request)) {
            puts("send failed");
            exit(1);
          }
        } else {
          send_package(sockfd, (struct sockaddr*)&serv_addr, ack);
        }
        continue;
      }
    } else if (Status::ServerErr == status) {
      printf("Receive a Server Error:\n");
      std::cout << (uint16_t)last_err_package.err_code << " "
                << last_err_package.err_msg << std::endl;
      goto ERROR_LABAL;
    } else {
      printf("Unknown Error");
      goto ERROR_LABAL;
    }
  }
OK_LABAL:
  printf("get file: %s Success!\n", filename);
  printf("%d Bytes!\n",sum_size);
  fclose(file);
  return;
ERROR_LABAL:
  puts("unexcepted exit!");
  remove(filename);
  fclose(file);
  return;
}
void put_file() {
  uint sum_size = 0;
  int sockfd;
  struct sockaddr_in serv_addr;
  FILE* file;
  static char buffer[k_buffer_size];
  if (strchr(filename, '/') != NULL) {
    printf(
        "do not support file transfer out of the current working directory\n");
    return;
  }
  file = fopen(filename, "rb");
  if (file == NULL) {
    perror(filename);
    return;
  }

  sockfd = host_UDPsocket(0);
  serv_addr = make_addr(serv_addr_s, serv_port);

  if (sockfd < 0) {
    printf("Couldn't open socket\n");
    return;
  }
  WriteRequest write_request;
  write_request.filename = std::string(filename);
  write_request.mode = std::string("octet");
  if (Status::Ok !=
      send_package(sockfd, (struct sockaddr*)&serv_addr, write_request)) {
    puts("send failed");
    exit(1);
  }
  uint timeout_cnt = 0;
  uint16_t block_num = 0;
  uint len = 0;
  Data data_package;
  while (1) {
    Ack ack_package;
    Status status =
        recv_package(sockfd, (struct sockaddr*)&serv_addr, ack_package);
    if (Status::Ok == status) {
      timeout_cnt = 0;
      if (ack_package.block_num == block_num) {
        sum_size += data_package.size();
        fprintf(stderr, "%d block ack\n", block_num);
        if (block_num != 0 && len < k_data_slice_size) goto OK_LABAL;
        len = fread(buffer, 1, k_data_slice_size, file);
        data_package.data.resize(len);
        for (int i = 0; i < len; i++) data_package.data[i] = buffer[i];
        data_package.block_num = ++block_num;
        send_package(sockfd, (struct sockaddr*)&serv_addr, data_package);
      } else {
        continue;
      }
    } else if (Status::Timeout == status) {
      fprintf(stderr, "%d block timeout\n", block_num);
      timeout_cnt++;
      if (timeout_cnt >= k_timeout_ms) {
        printf("There have been %d times timeout\n", timeout_cnt);
        goto ERROR_LABAL;
      } else {
        if (block_num == 0) {
          if (Status::Ok != send_package(sockfd, (struct sockaddr*)&serv_addr,
                                         write_request)) {
            puts("send failed");
            exit(1);
          }
        } else {
          send_package(sockfd, (struct sockaddr*)&serv_addr, data_package);
        }
        continue;
      }
    } else if (Status::ServerErr == status) {
      printf("Receive a Server Error:\n");
      std::cout << (uint16_t)last_err_package.err_code << " "
                << last_err_package.err_msg << std::endl;
      goto ERROR_LABAL;
    } else {
      printf("Unknown Error");
      goto ERROR_LABAL;
    }
  }
OK_LABAL:
  printf("put file: %s Success!\n", filename);
  printf("%d Bytes!\n",sum_size);
  fclose(file);
  return;
ERROR_LABAL:
  puts("unexcepted exit!");
  fclose(file);
  return;
}

void print_help() { puts("Usage: ./tftpclient get|put addr port filename"); }

int main(int argc, char* argv[]) {
  srand(time(0));
  if (argc != 5) {
    print_help();
    return 1;
  }

  if (strcmp(argv[1], "get") == 0) {
    isGet = true;
  } else if (strcmp(argv[1], "put") == 0) {
    isGet = false;
  } else {
    print_help();
    return 1;
  }
  serv_addr_s = argv[2];
  serv_port = (uint16_t)atoi(argv[3]);
  filename = argv[4];
  if (isGet)
    get_file();
  else
    put_file();
  return 0;
}