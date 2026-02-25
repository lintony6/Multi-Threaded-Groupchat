/*
 * Implementation of a two-way async message client in C
 */

/* Include header files for socket related functions */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Maximum message length */
#define MAXMESGLEN 1024

/*
 * Read a message from the socket
 * Put the message in the given buffer
 * Return NULL if nothing to read
 * Otherwise, return the message
 */
char *recvMesg(int sd, char *mesg, size_t maxLen) {
  /* Keep reading one char at a time */
  char *ptr = mesg;

  // Add bounds checking to prevent segfault
  size_t charsRead = 0;
  while (charsRead < maxLen - 1) {
    /* Read one char into the buffer */
    int nread = read(sd, ptr, sizeof(char));

    /* If errors, report and exit */
    if (nread == -1) {
      perror("socket");
      exit(1);
    }

    /* If no byte read, i.e., EOF, return NULL */
    if (nread == 0)
      return (NULL);

    /* If newline character, thats the end of message */
    if (*ptr == '\n') {
      *ptr++ = '\n';
      /* string should end with a null character */
      break;
    }

    /* Advance the pointer to place the next char */
    ptr++;
    charsRead++;
  }

  *ptr = '\0';
  /* Return the message */
  return (mesg);
}

/*
 * The client program starts from here
 */
int main(int argc, char *argv[]) {
  /* Client needs server's contact information */
  if (argc != 4) {
    fprintf(stderr, "usage : %s <client name> <server name> <server port>\n",
            argv[0]);
    exit(1);
  }

  if (strlen(argv[1]) > 19) {
    perror("Name too long, maximum 19 chars");
    return -1;
  }

  /* Get server whereabouts */
  char *serverName = argv[2];
  int serverPort = atoi(argv[3]);

  /* Create a socket */
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == -1) {
    perror("socket");
    exit(1);
  }

  /* Fill the server address structure */
  struct sockaddr_in serverAddr;
  bzero((char *)&serverAddr, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(serverPort);

  /* Get the IP address corresponding to server host */
  struct hostent *hostEntry = gethostbyname(serverName);
  if (!hostEntry) {
    perror(serverName);
    exit(1);
  }
  bcopy(hostEntry->h_addr, (char *)&serverAddr.sin_addr, hostEntry->h_length);

  /* Connect to the server */
  if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
    perror("connect");
    exit(1);
  }
  printf("Connected to server at ('%s', '%d')\n", serverName, serverPort);

  // Send server name upon connecting
  write(sock, argv[1], strlen(argv[1]));
  write(sock, "\n", 1);
  /* Keep sending and receiving messages from the client */
  char buffer[MAXMESGLEN];
  // Create 2 element array to use poll()
  struct pollfd fds[2];
  fds[0].fd = 0;
  fds[0].events = POLLIN;

  fds[1].fd = sock;
  fds[1].events = POLLIN;

  printf("%s: ", argv[1]);
  fflush(stdout);
  /* Keep connection open to server */
  while (1) {
    // Wait indefinently until input received from sock or stdin
    int action = poll(fds, 2, -1);
    if (action == -1) {
      fprintf(stderr, "poll error");
      continue;
    }
    if (action > 0) {
      // Input received from stdin, need to write
      if (fds[0].revents & POLLIN) {
        /* Read a message from the keyboard */
        char *line = fgets(buffer, MAXMESGLEN, stdin);

        /* If EOF, close the connection */
        if (line == NULL) {
          printf("Closing connection\n");
          close(sock);
          return 0;
        }
        /* Send the line to the server */
        if (write(sock, line, strlen(line)) < 0) {
          fprintf(stderr, "write");
        }
        printf("%s: ", argv[1]);
        fflush(stdout);
      }

      // Input received from socket, need to read.
      if (fds[1].revents & POLLIN) {
        /* Wait to receive a message */
        char *message = recvMesg(sock, buffer, MAXMESGLEN);
        /* If message is NULL ==> connection was closed */
        if (message == NULL) {
          printf("Server closed connection\n");
          close(sock);
          return 0;
        }
        /* Display the message */
        printf("\n%s", message);
        printf("%s: ", argv[1]);
        fflush(stdout);
      }
      // Check for closed connection or error
      if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        close(fds[1].fd);
        return 0;
      }
    }
  }
  return 0;
}
