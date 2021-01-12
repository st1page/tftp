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
#include "socket_utils.h"

using namespace std;

bool isGet;
char* serv_addr_s;
uint16_t serv_port;
char* filename;

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