cmake_minimum_required(VERSION 3.16)
project(SecureChat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Один исполняемый файл для сервера и клиента
add_executable(chat
    main.cpp
    Socket.cpp
    TcpServer.cpp
    Protocol.cpp
    Server.cpp
    Client.cpp
)

# Подключаем потоки (нужны для std::thread и std::mutex)
find_package(Threads REQUIRED)
target_link_libraries(chat Threads::Threads)