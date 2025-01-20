#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

#define BUFFER_SIZE 1024

int client_socket;
int running = 1;                // Global flag to control the receive thread
int socket_closed = 0;          // Flag to track if the socket is already closed
int intentional_disconnect = 0; // Flag to track if the disconnect was intentional

void *receive_messages(void *arg);
void print_help();

// Signal handler for Ctrl+C
void handle_sigint(int sig)
{
    printf("\nClient terminated by user.\n");
    running = 0; // Signal the receive thread to exit

    if (!socket_closed)
    {
        close(client_socket); // Close the socket if it's not already closed
        socket_closed = 1;
    }
    exit(0); // Exit the program
}

int main(int argc, char *argv[])
{
    // Register the SIGINT handler
    signal(SIGINT, handle_sigint);

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <ip_address> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *ip_address = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in server_addr;

    // Create socket
    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server\n");

    // Create a thread to receive messages from the server
    pthread_t tid;
    if (pthread_create(&tid, NULL, receive_messages, &client_socket) != 0)
    {
        perror("Thread creation failed");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // Main input loop
    char buffer[BUFFER_SIZE];
    while (running)
    {
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline

        if (strcmp(buffer, "/help") == 0)
        {
            print_help();
        }
        else if (strcmp(buffer, "/quit") == 0)
        {
            printf("Disconnecting from the server...\n");
            running = 0;                // Signal the receive thread to exit
            intentional_disconnect = 1; // Mark the disconnect as intentional

            // Send the quit command to the server
            if (send(client_socket, buffer, strlen(buffer), 0) < 0)
            {
                perror("Send failed");
            }

            // Wait for the server's response before closing the socket
            char response[BUFFER_SIZE];
            int bytes_received = recv(client_socket, response, sizeof(response) - 1, 0);
            if (bytes_received > 0)
            {
                response[bytes_received] = '\0';
                printf("%s\n", response); // Print the server's goodbye message
            }

            if (!socket_closed)
            {
                close(client_socket); // Close the socket if it's not already closed
                socket_closed = 1;
            }
            break;
        }
        else if (send(client_socket, buffer, strlen(buffer), 0) < 0)
        {
            perror("Send failed");
            break;
        }
    }

    if (!socket_closed)
    {
        close(client_socket); // Close the socket if it's not already closed
        socket_closed = 1;
    }

    pthread_join(tid, NULL);
    printf("Client terminated.\n");
    return 0;
}

// Thread function to receive messages from the server
void *receive_messages(void *arg)
{
    int socket = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while (running && (bytes_read = recv(socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_read] = '\0';
        printf("%s\n", buffer);

        // Check if the server is shutting down
        if (strstr(buffer, "[SERVER]: The server is shutting down.") != NULL)
        {
            printf("Server is shutting down. Disconnecting...\n");
            running = 0;
            break;
        }
    }

    if (bytes_read == 0 && !intentional_disconnect)
    {
        // Only print "Server disconnected" if the disconnect was not intentional
        printf("Server disconnected.\n");
    }
    else if (bytes_read < 0)
    {
        perror("Receive failed");
    }

    if (!socket_closed)
    {
        close(socket); // Close the socket if it's not already closed
        socket_closed = 1;
    }

    pthread_exit(NULL);
}

// Print help instructions
void print_help()
{
    printf("\a"); // This will produce a beep sound in the terminal
    printf("\n[CLIENT HELP]:\n"
           "/help - Show this help message\n"
           "/list - List all connected clients\n"
           "/private <username> <message> - Send a private message to a user\n"
           "/quit - Disconnect from the server\n\n");
}