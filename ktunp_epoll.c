#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <sys/epoll.h>

#define SYSFS_KTUNP_NEXTHOP  "/sys/ktunp/nexthop"
#define SYSFS_KTUNP_INETADDR "/sys/ktunp/inetaddr"
#define DATA_SIZE 100
#define MAX_EVENTS 10

int main(int argc, char **argv)
{
  int nread;
  int nexthop_fd;
  int inetaddr_fd;
  char data[DATA_SIZE];
  int epoll_fd;
  int nfds;
  struct epoll_event ev, events[MAX_EVENTS];

  epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    fprintf(stderr, "Error: epoll()\n");
    return EXIT_FAILURE;
  }

  nexthop_fd = open(SYSFS_KTUNP_NEXTHOP, O_RDONLY);
  if (nexthop_fd < 0) {
    fprintf(stderr, "Error: open() nexthop\n");
    return EXIT_FAILURE;
  }
  ev.events = EPOLLIN|EPOLLET;
  ev.data.fd = nexthop_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nexthop_fd, &ev) < 0) {
    fprintf(stderr, "Error: epoll_ctl() nexthop\n");
    return EXIT_FAILURE;
  }

  inetaddr_fd = open(SYSFS_KTUNP_INETADDR, O_RDONLY);
  if (inetaddr_fd < 0) {
    fprintf(stderr, "Error: open() inetaddr\n");
    return EXIT_FAILURE;
  }
  ev.events = EPOLLIN|EPOLLET;
  ev.data.fd = inetaddr_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inetaddr_fd, &ev) < 0) {
    fprintf(stderr, "Error: epoll_ctl() inetaddr\n");
    return EXIT_FAILURE;
  }

  for (;;) {
    int n;

    nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      fprintf(stderr, "Error: epoll_wait()\n");
      return EXIT_FAILURE;
    }

    for (n = 0; n < nfds; ++n) {
      if (events[n].data.fd == nexthop_fd) {
        nread = read(nexthop_fd, data, DATA_SIZE);
        printf("nread nexthop = %d\n", nread);
      } else if (events[n].data.fd == inetaddr_fd) {
        nread = read(inetaddr_fd, data, DATA_SIZE);
        printf("nread inetaddr = %d\n", nread);
      }
    }
  };

  /* Never reach here, though. */
  close(inetaddr_fd);
  close(nexthop_fd);
  close(epoll_fd);

  return EXIT_SUCCESS;
}
