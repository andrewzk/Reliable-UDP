/* 
 * vs_send: A simple RUDP sender that can be used to transfer files.
 * Arguments: destination address * (dot quadded or host.domain),  
 * remote port number, and a list of files
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "rudp_api.h"
#include "event.h"
#include "vsftp.h"

#define MAXPEERS 32  /* Max number of remote peers */
#define MAXPEERNAMELEN 256  /* Max length of peer name */

/* Prototypes */
int usage();
int filesender(int fd, void *arg);
void send_file(char *filename);
int eventhandler(rudp_socket_t rsocket, rudp_event_t event, struct sockaddr_in *remote);

/* Global variables */
int debug = 0;  /* Debug flag */
struct sockaddr_in peers[MAXPEERS];  /* IP address and port */
int npeers = 0;  /* Number of elements in peers */

/* usage: how to use program */
int usage() {
  fprintf(stderr, "Usage: vs_send [-d] host1:port1 [host2:port2] ... file1 [file2]... \n");
  exit(1);
}

int main(int argc, char* argv[]) {
  int port;
  char *hoststr;
  struct hostent* hp;
  struct in_addr *addr;
  int c;
  int i;

  /* Parse and collect arguments */
  opterr = 0;

  while ((c = getopt(argc, argv, "d")) != -1) {
    if (c == 'd') {
      debug = 1;
    }
    else 
      usage();
  }

  for (i = optind; i < argc; i++) {
    /* found last host:port? */
    if (strchr(argv[i], ':') == NULL)
      break;
    /* Make a copy of argument string that we can modify  */
    hoststr = (char *) malloc(strlen(argv[i]) + 1);
    if (hoststr == NULL) {
      fprintf(stderr, "vs_send: malloc failed\n");
      exit(1);
    }
    strcpy(hoststr, argv[i]);
    port = atoi(strchr(hoststr, ':') + 1);
    if (port <= 0) {
      fprintf(stderr, "Bad destination port: %d\n", 
        atoi(strchr(hoststr, ':') + 1));
      exit(1);
    }
    *strchr(hoststr, ':') = '\0';
    if ((hp = gethostbyname(hoststr)) == NULL || 
      (addr = (struct in_addr *)hp->h_addr) == NULL) {
      fprintf(stderr,"Can't locate host \"%s\"\n", hoststr); 
      return(0);
    }
    memset((char *)&peers[npeers], 0, sizeof(struct sockaddr_in));
    peers[npeers].sin_family = AF_INET;
    peers[npeers].sin_port = htons(port);
    memcpy(&peers[npeers].sin_addr, addr, sizeof(struct in_addr));
    npeers++;
    free(hoststr);
  }
  /* Need at least one peer */
  if (npeers == 0)
    usage();

  if (optind >= argc) {
    usage();
  }

  /* Launch senders for each file */
  while (i < argc) { 
    send_file(argv[i++]);
  }

  eventloop(0);
  return 0;
}

/* 
 * eventhandler: callback function for RUDP events
 */

int eventhandler(rudp_socket_t rsocket, rudp_event_t event, struct sockaddr_in *remote) {
  
  switch (event) {
  case RUDP_EVENT_TIMEOUT:
    if (remote) {
      fprintf(stderr, "rudp_sender: time out in communication with %s:%d\n",
        inet_ntoa(remote->sin_addr),
        ntohs(remote->sin_port));
    }
    else {
      fprintf(stderr, "rudp_sender: time out\n");
    }
    exit(1);
    break;
  case RUDP_EVENT_CLOSED:
    if (debug) {
      fprintf(stderr, "rudp_sender: socket closed\n");
    }
    break;
  }
  return 0;
}

/*
 * send_file: initiate sending of a file. 
 * Create a RUDP socket for sending. Send the file name to the VS receiver.
 * Register a handler for input event, which will take care of sending
 * file data
 */

void send_file(char *filename) {
  struct vsftp vs;
  int vslen;
  char *filename1;
  int namelen;
  int file = 0;
  int p;
  rudp_socket_t rsock;

  if ((file = open(filename, O_RDONLY)) < 0) {
    perror("vs_sender: open");
    exit(-1);
  }
  rsock = rudp_socket(0);
  if (rsock == NULL) {
    fprintf(stderr, "vs_send: rudp_socket() failed\n");
    exit(1);
  }
  rudp_event_handler(rsock, eventhandler);

  vs.vs_type = htonl(VS_TYPE_BEGIN);

  /* strip of any leading path name */
  filename1 = filename;
  if (strrchr(filename1, '/'))
    filename1 = strrchr(filename1, '/') + 1;
  
  /* Copy file name into VS data */
  namelen = strlen(filename1) < VS_FILENAMELENGTH  ? strlen(filename1) : VS_FILENAMELENGTH;
  strncpy(vs.vs_info.vs_filename, filename1, namelen);

  vslen = sizeof(vs.vs_type) + namelen;
  for (p = 0; p < npeers; p++) {
    if (debug) {
      fprintf(stderr, "vs_send: send BEGIN \"%s\" (%d bytes) to %s:%d\n",
        filename, vslen, 
        inet_ntoa(peers[p].sin_addr), ntohs(peers[p].sin_port));
    }
    if (rudp_sendto(rsock, (char *) &vs, vslen, &peers[p]) < 0) {
      fprintf(stderr,"rudp_sender: send failure\n");
      rudp_close(rsock);    
      return;
    }
  }
  event_fd(file, filesender, rsock, "filesender");
}

/*
 * filesender: callback function for handling sending of the file.
 * Will be called when data is available on the file (which is always
 * true, until the file is closed...). 
 * Send file data. Detect end of file and tell VS peers that transfer is
 * complete
 */

int filesender(int file, void *arg) {
  rudp_socket_t rsock =   (rudp_socket_t) arg;
  int bytes;
  struct vsftp vs;
  int vslen;
  int p;

  bytes = read(file, &vs.vs_info.vs_data,VS_MAXDATA);
  if (bytes < 0) {
  perror("filesender: read");
  event_fd_delete(filesender, rsock);
  rudp_close(rsock);    
  }
  else if (bytes == 0) {
  vs.vs_type = htonl(VS_TYPE_END);
  vslen = sizeof(vs.vs_type);
  for (p = 0; p < npeers; p++) {
    if (debug) {
    fprintf(stderr, "vs_send: send END (%d bytes) to %s:%d\n", 
      vslen, inet_ntoa(peers[p].sin_addr), htons(peers[p].sin_port));
    }
    if (rudp_sendto(rsock, (char *) &vs, vslen, &peers[p]) < 0) {
    fprintf(stderr,"rudp_sender: send failure\n");
    break;
    }
  }
  event_fd_delete(filesender, rsock);
  rudp_close(rsock);    
  }
  else {
  vs.vs_type = htonl(VS_TYPE_DATA);
  vslen = sizeof(vs.vs_type) + bytes;
    for (p = 0; p < npeers; p++) {
      if (debug) {
        fprintf(stderr, "vs_send: send DATA (%d bytes) to %s:%d\n", 
        vslen, inet_ntoa(peers[p].sin_addr), htons(peers[p].sin_port));        
      }
      if (rudp_sendto(rsock, (char *) &vs, vslen, &peers[p]) < 0) {
        fprintf(stderr,"rudp_sender: send failure\n");
        event_fd_delete(filesender, rsock);
        rudp_close(rsock);    
        break;
      }
    }
  }
  return 0;
}


