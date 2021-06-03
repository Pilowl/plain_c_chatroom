#include <arpa/inet.h>

#include "common.h"

int sockfd;

static volatile int server_is_responding = 0;

void str_overwrite_stdout() {
  printf("%s", "> ");
  fflush(stdout);
}

void send_handler()
{
    char buffer[MSG_LENGTH + 2] = {};
    char name[32] = {};
    for(;;)
    {
        bzero(buffer, strlen(buffer));
        str_overwrite_stdout();
        fgets(buffer, MSG_LENGTH + 2, stdin);
        if (strlen(buffer) > MSG_LENGTH)
        {
            printf("ERROR: Max message length is %d\n", MSG_LENGTH);
            while ((getchar()) != '\n');
            continue;
        }
        send(sockfd, buffer, strlen(buffer), 0);
    }
}

void receive_handler()
{
    char message[MSG_LENGTH + 50] = {};
    for (;;)
    {
        int receive = recv(sockfd, message, MSG_LENGTH + 50, 0);
        if (receive > 0) {
            printf("%s\n", message);
            str_overwrite_stdout();
        }
        else
        {
            server_is_responding = 0;
            break;
        }
        memset(message, 0, sizeof(message));
        sleep(1);
    }
    pthread_detach(pthread_self());
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: ./client <server_port>\n");
        exit(0);
    }

    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);
    
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

    server_is_responding = 1;

    pthread_t send_msg_thread;
    pthread_create(&send_msg_thread, NULL, (void *) send_handler, NULL);

    pthread_t recv_msg_thread;
    pthread_create(&recv_msg_thread, NULL, (void *) receive_handler, NULL);
    while (server_is_responding) {}
    printf("Connection to server is lost.\n");
    close(sockfd);
    pthread_cancel(send_msg_thread);
}
