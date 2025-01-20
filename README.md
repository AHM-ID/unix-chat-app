# UNIX Chat Application

A simple and robust chat application implementing server-client communication for real-time messaging with support for multiple clients.

## Features

### Server
- Handles up to 10 clients simultaneously.
- Enforces unique usernames.
- Supports broadcast and private messaging.
- Offers administrative commands for managing clients and shutting down the server gracefully.

### Client
- Connects to the server using an IP address and port.
- Sets unique usernames for identification.
- Sends broadcast and private messages.
- Lists all connected users.
- Gracefully disconnects with `/quit`.

## Prerequisites
- **GCC Compiler**: To compile the source code.
- **Linux Environment**: Utilizes POSIX threads and sockets.

## Project Structure
```
unix-chat-app/
├── src/
│   ├── client.c
│   ├── server.c
├── obj/
│   ├── client.o
│   ├── server.o
├── bin/
│   ├── client
│   ├── server
├── Makefile/
│   ├── Makefile
├── LICENSE
└── README.md
```

## Cloning the Repository
To clone this project, run:
```bash
git clone https://github.com/AHM-ID/unix-chat-app.git
cd unix-chat-app
```

## Compilation

### Build
Run the following in the project directory:
```bash
make all
```
This compiles the client and server and places executables in the `bin/` directory.

### Clean
To clean the build:
```bash
make clean
```

## Usage
Navigate to the `bin/` directory to run the server and client:
```bash
cd bin
```

### Starting the Server
```bash
./server [port]
```
- Default port: `8080`.

Example:
```bash
./server 3000
```

### Starting the Client
```bash
./client <ip_address> <port>
```
Example:
```bash
./client 127.0.0.1 8080
```
Or
```bash
./client $(hostname -I | awk '{print $1}') 3000
```

## Commands

### Client Commands
| Command                         | Description                           |
|---------------------------------|---------------------------------------|
| `/help`                         | Show available commands.              |
| `/username <name>`              | Set your username.                    |
| `/list`                         | List connected users.                 |
| `/private <username> <message>` | Send a private message.               |
| `/quit`                         | Disconnect.                           |

### Server Commands
| Command                         | Description                           |
|---------------------------------|---------------------------------------|
| `/help`                         | Show available commands.              |
| `/list`                         | List connected clients.               |
| `/message <msg>`                | Broadcast a message.                  |
| `/private <username> <msg>`     | Private message a client.             |
| `/remove <username>`            | Disconnect a client.                  |
| `/shutdown`                     | Shut down the server.                 |

## Example
1. Navigate to the `bin/` directory:
   ```bash
   cd bin
   ```
2. Start the server:
   ```bash
   ./server 8080
   ```
3. Start a client:
   ```bash
   ./client 127.0.0.1 8080
   ```

## License

This project is licensed under the [MIT License](https://opensource.org/licenses/MIT).
