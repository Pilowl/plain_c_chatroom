#include <signal.h>
#include <arpa/inet.h>

#include "common.h"

int sockfd;

void str_overwrite_stdout() {
  printf("%s", "> ");
  fflush(stdout);
}

void send_handler()
{
    char buffer[MSG_LENGTH] = {};
    char name[32] = {};
    for (;;)
    {
        bzero(buffer, MSG_LENGTH);
        str_overwrite_stdout();
        fgets(buffer, MSG_LENGTH, stdin);
        send(sockfd, buffer, strlen(buffer), 0);
    }
}

void receive_handler()
{
    char message[MSG_LENGTH] = {};
    for (;;)
    {
        int receive = recv(sockfd, message, MSG_LENGTH, 0);
        if (receive > 0) {
            printf("%s\n", message);
            str_overwrite_stdout();
        }
        memset(message, 0, sizeof(message));
    }
}

int main()
{
    char *ip = "127.0.0.1";
    int port = 1234;
    
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0)
    {
        printf("Failed to initialize socket connection...\n");
        return 1;
    }

    pthread_t send_msg_thread;
    pthread_create(&send_msg_thread, NULL, (void *) send_handler, NULL);

    pthread_t recv_msg_thread;
    pthread_create(&recv_msg_thread, NULL, (void *) receive_handler, NULL);
    for (;;) {}
    close(sockfd);
}
