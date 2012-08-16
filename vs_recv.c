/* 
 * A simple RUDP receiver to receive files from remote hosts.
 * It takes only one argument - local port to be used.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rudp_api.h" 
#include "event.h" 
#include "vsftp.h"

/*
 * Data structure for keeping track of partially received files 
 */

struct rxfile {
  struct rxfile *next;  /* Next pointer for linked list */
  int fileopen;    /* True if file is open */
  int fd;    /* File descriptor */
  struct sockaddr_in remote;  /* Peer */
  char name[VS_FILENAMELENGTH+1]; /* Name of file */

};

/* 
 * Prototypes 
 */

int filesender(int fd, void *arg);
int rudp_receiver(rudp_socket_t rsocket, struct sockaddr_in *remote, char *buf, int len);
int eventhandler(rudp_socket_t rsocket, rudp_event_t event, struct sockaddr_in *remote);
int usage();

/* 
 * Global variables 
 */
int debug = 0;    /* Print debug messages */
struct rxfile *rxhead = NULL;  /* Pointer to linked list of rxfiles */

/* 
 * usage: how to use program
 */

int usage() {
  fprintf(stderr, "Usage: vs_recv [-d] port\n");
  exit(1);
}

int main(int argc, char* argv[]) {
  rudp_socket_t rsock;
  int port;

  int c;

  /* 
   * Parse and collect arguments
   */
  opterr = 0;

  while ((c = getopt(argc, argv, "d")) != -1) {
  if (c == 'd') {
    debug = 1;
  }
  else 
    usage();
  }
  if (argc - optind != 1) {
  usage();
  }
  
  port = atoi(argv[optind]);
  if (port <= 0) {
  fprintf(stderr, "Bad destination port: %s\n", argv[optind]);
  exit(1);
  }

  if (debug) {
  printf("RUDP receiver waiting on port %i.\n",port);
  }

  /*
   * Create RUDP listener socket
   */

  if ((rsock = rudp_socket(port)) == NULL) {
  fprintf(stderr,"vs_recv: rudp_socket() failed\n");
  exit(1);
  }

  /*
   * Register receiver callback function
   */

  rudp_recvfrom_handler(rsock, rudp_receiver);

  /*
   * Register event handler callback function
   */

  rudp_event_handler(rsock, eventhandler);

  /*
   * Hand over control to event manager
   */

  eventloop(0);

  return (0);
}

/*
 * rxfind: helper function to lookup a rxfile descriptor on the linked list.
 * Create new if not found
 */

static struct rxfile *rxfind(struct sockaddr_in *addr) {
  struct rxfile *rx;

  for (rx = rxhead; rx != NULL; rx = rx->next) {
  if (memcmp(&rx->remote, addr, sizeof(struct in_addr)) == 0)
    return rx;
  }
  /* Not found, create new */
  if ((rx = malloc(sizeof(struct rxfile))) == NULL) {
  fprintf(stderr, "vs_receiver: malloc failed\n");
  exit(1);
  }
  rx->fileopen = 0;
  rx->remote = *addr;
  rx->next = rxhead;
  rxhead = rx;
  return rx;
}

/*
 * rxdel: helper function to unlink and free a rxfile descriptor
 */

static int rxdel(struct rxfile *rx) {
  struct rxfile **rxp;

  for (rxp = &rxhead; *rxp != NULL && *rxp != rx; rxp = &(*rxp)->next)
  ;
  if (*rxp == NULL) { /* Not found */
  fprintf(stderr, "vs_recv: Can't find rx record for peer\n");
  return -1;
  }
  *rxp = rx->next;
  free(rx);
  return 0;
}


/* 
 * eventhandler: callback function for RUDP events
 */

int eventhandler(rudp_socket_t rsocket, rudp_event_t event, struct sockaddr_in *remote) {
  struct rxfile *rx;

  switch (event) {
  case RUDP_EVENT_TIMEOUT:
  if (remote) {
    fprintf(stderr, "vs_recv: time out in communication with %s:%d\n",
    inet_ntoa(remote->sin_addr),
    ntohs(remote->sin_port));
    if ((rx = rxfind(remote))) {
    if (rx->fileopen) {
      close(rx->fd);
    }
    rxdel(rx);
    }
  }
  else {
    fprintf(stderr, "vs_recv: time out\n");
  }
  break;
  case RUDP_EVENT_CLOSED:
  if (remote && (rx = rxfind(remote))) {
    if (rx->fileopen) {
    fprintf(stderr, "vs_recv: prematurely closed communication with %s:%d\n",
      inet_ntoa(remote->sin_addr),
      ntohs(remote->sin_port));
    close(rx->fd);
    }
    rxdel(rx);
  } /* else ignore */
    break;
  default:
  fprintf(stderr, "vs_recv: unknown event %d\n", event);
  break;
}
  return 0;
}

/*
 * rudp_receiver: callback function for processing data received
 * on RUDP socket.
 */

int rudp_receiver(rudp_socket_t rsocket, struct sockaddr_in *remote, char *buf, int len) {
  struct rxfile *rx;
  int namelen;
  int i;

  struct vsftp *vs = (struct vsftp *) buf;
  if (len < VS_MINLEN) {
  fprintf(stderr, "vs_recv: Too short VSFTP packet (%d bytes)\n",
    len);
  return 0;
  }
  rx = rxfind(remote);
  switch (ntohl(vs->vs_type)) {
  case VS_TYPE_BEGIN:
  namelen = len - sizeof(vs->vs_type);
  if (namelen > VS_FILENAMELENGTH)
    namelen = VS_FILENAMELENGTH;
  strncpy(rx->name, vs->vs_info.vs_filename, namelen);
  rx->name[namelen] = '\0'; /* Null terminated */

  /* Verify that file name is valid
   * Only alpha-numerical, period, dash and
   * underscore are allowed */
  for (i = 0; i < namelen; i++) {
    char c = rx->name[i];
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) {
    fprintf(stderr, "vs_recv: Illegal file name \"%s\"\n", 
      rx->name);
    rudp_close(rsocket);
    return 0;
    }
  }

  if (debug) {
    fprintf(stderr, "vs_recv: BEGIN \"%s\" (%d bytes) from %s:%d\n", rx->name, len,
    inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
  }
  if ((rx->fd = creat(rx->name, 0644)) < 0) {
    perror("vs_recv: create");
    rudp_close(rsocket);
  }
  else {
    rx->fileopen = 1;
  }
  break;
  case VS_TYPE_DATA:
  if (debug) {
    fprintf(stderr, "vs_recv: DATA (%d bytes) from %s:%d\n", 
    len, 
    inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
  }
  len -= sizeof(vs->vs_type);
  /* len now is length of payload (data or file name) */
  if (rx->fileopen) {
    if ((write(rx->fd, vs->vs_info.vs_filename, len)) < 0) {
    perror("vs_recv: write");
    }
  }
  else {
    fprintf(stderr, "vs_recv: DATA ignored (file not open)\n");
  }
  break;
  case VS_TYPE_END:
  if (debug) {
    fprintf(stderr, "vs_recv: END (%d bytes) from %s:%d\n",
    len, inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
  }
  printf("vs_recv: received end of file \"%s\"\n", rx->name);
  if (rx->fileopen) {
    close(rx->fd);
    rxdel(rx);
  }
  /* else ignore */
  break;
  default:
  fprintf(stderr, "vs_recv: bad vsftp type %d from %s:%d\n",
    vs->vs_type, inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
  }
  return 0;
}

