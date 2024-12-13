#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <wait.h>
#include <memory.h>
#include <syscall.h>
#include <errno.h>
#include "util.h"
#include "netns.h"




#define STACK_SIZE (1024*1024)

static int child_proc(void* args) // Функция дочернего процесса
{
    printf("child start\n"); // Вывод сообщения о начале работы дочернего процесса

    sleep(1); // Пауза в выполнении на 1 секунду

    int mysock = socket(AF_INET, SOCK_DGRAM, 0); // Создание сокета для UDP-соединения
    if (mysock == -1 ) { // Проверка на успешное создание сокета
        perror("child: create socket error"); // Вывод сообщения об ошибке
        exit(1); // Завершение программы с ошибкой
    } else {
        printf("child: create socket OK\n"); // Вывод сообщения об успешном создании сокета
    }

    int optval = 1; // Опция для настройки сокета
    if (-1 == setsockopt(mysock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))){ // Установка опции для сокета
        perror("child: setsockopt error"); // Вывод сообщения об ошибке
        close(mysock); // Закрытие сокета
        exit(1); // Завершение программы с ошибкой
    } else {
        printf("child: setsockopt OK\n"); // Вывод сообщения об успешной установке опции
    }

    /* привяжем сокет потомка к порту */
    struct sockaddr_in bindaddr; // Структура для адреса привязки
    bindaddr.sin_family = AF_INET; // Установка типа адреса (IPv4)
    bindaddr.sin_port = htons(8000); // Установка порта (8000) с преобразованием в сетевой порядок байт
    bindaddr.sin_addr.s_addr = inet_addr("10.1.1.2"); // Установка IP-адреса потомка

    if (-1 == bind(mysock, (struct sockaddr*)&bindaddr, sizeof(bindaddr))){ // Привязка сокета к адресу
        perror("child: bind socket error"); // Вывод сообщения об ошибке
        close(mysock); // Закрытие сокета
        exit(1); // Завершение программы с ошибкой
    } else {
        printf("child: bind socket OK\n"); // Вывод сообщения об успешной привязке
    }

    /*адрес родителя*/
    struct sockaddr_in saddr; // Структура для хранения адреса родителя
    socklen_t saddrlen; // Переменная для хранения размера адресной структуры
    memset(&saddr, 0, sizeof(saddr)); // Обнуление структуры адреса
    saddr.sin_family = AF_INET; // Установка типа адреса (IPv4)
    saddr.sin_port = htons(8000); // Установка порта (8000) с преобразованием в сетевой порядок байт
    saddr.sin_addr.s_addr = inet_addr("10.1.1.1"); // Установка IP-адреса родителя
    saddrlen = sizeof(saddr); // Установка размера структуры адреса


    char sndbuf[256]; // Буфер для отправки данных
    memset(sndbuf, 0, sizeof(sndbuf)); // Обнуление буфера отправки
    int len = sprintf(sndbuf, "hello, parent!"); // Формирование сообщения "hello, parent!" и сохранение его длины
    int sentcount = sendto(mysock, sndbuf, len, 0, (struct sockaddr*)&saddr, saddrlen); // Отправка сообщения родительскому процессу
    if (sentcount == -1) {
        perror("child: send error"); // Вывод сообщения об ошибке отправки
    } else {
        printf("child: sendto OK: sndbuf = %s; sentcount = %d\n", sndbuf, sentcount); // Вывод сообщения об успешной отправке
    }

    char rcvbuf[256]; // Буфер для приема данных
    memset(rcvbuf, 0, sizeof(rcvbuf)); // Обнуление буфера приема
    saddrlen = sizeof(saddr); // Установка размера структуры адреса
    int recvcount = recvfrom(mysock, rcvbuf, sizeof(rcvbuf), 0, (struct sockaddr*)&saddr, &saddrlen); // Прием данных от родительского процесса
    printf("child: parent: veth0 ip = %s; port = %d\n", inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port)); // Вывод IP-адреса и порта родителя
    if (recvcount == -1) {
        perror("child: recv error"); // Вывод сообщения об ошибке приема
    } else if (recvcount == 0) {
        printf("child: reccount = 0\n"); // Вывод сообщения о нулевом количестве принятых данных
    } else {
        printf("child: recvfrom OK: rcvbuf = %s; recvcount = %d\n", rcvbuf, recvcount); // Вывод сообщения об успешном приеме данных
    }

    printf("child: close mysock = %d\n", close(mysock)); // Закрытие сокета и вывод результата закрытия

    printf("child finish\n"); // Вывод сообщения о завершении работы дочернего процесса

    //system("ip addr show veth1"); // Команда для отображения информации о сетевом интерфейсе 

    return 0; // Возврат нуля при успешном завершении функции
}

static void prepare_netns(int cmd_pid)
{
    char *veth = "veth0"; // Имя одного из виртуальных Ethernet-интерфейсов

    char *vpeer = "veth1"; // Имя другого виртуального Ethernet-интерфейса
    char *veth_addr = "10.1.1.1";  // IP-адрес для интерфейса veth

    char *vpeer_addr = "10.1.1.2"; // IP-адрес для интерфейса vpeer
    
    char *netmask = "255.255.255.0"; // Маска подсети

    int sock_fd = create_socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE); // Создание сокета для работы с маршрутизацией

    create_veth(sock_fd, veth, vpeer); // Создание пары виртуальных Ethernet-интерфейсов

    if_up(veth, veth_addr, netmask);  // Включение интерфейса veth и назначение ему IP-адреса

    int mynetns = get_netns_fd(getpid()); // Получение дескриптора файла для текущего сетевого пространства имен

    int child_netns = get_netns_fd(cmd_pid); // Получение дескриптора файла для сетевого пространства имен дочернего процесса

    move_if_to_pid_netns(sock_fd, vpeer, child_netns);  // Перемещение интерфейса vpeer в сетевое пространство имен дочернего процесса

    if (setns(child_netns, CLONE_NEWNET)){ // Переключение в сетевое пространство имен дочернего процесса
        printf("Failed to setns for command at pid %d: %m\n", cmd_pid); // Вывод сообщения об ошибке
        exit(1); // Завершение программы с кодом ошибки 1
    }

    if_up(vpeer, vpeer_addr, netmask); // Включение интерфейса vpeer и назначение ему IP-адреса

    if (setns(mynetns, CLONE_NEWNET)){  // Возврат в исходное сетевое пространство имен
       printf("Failed to restore previous net namespace: %m\n"); // Вывод сообщения об ошибке
       exit(1); // Завершение программы с кодом ошибки 1
    }

    close(sock_fd); // Закрытие сокета

}

int main(int argc, char* argv[]) // Основная функция программы
{
    char* stack; // Указатель на начало стека
    char* stackTop; // Указатель на вершину стека
    char buf[1024]; // Буфер для временного хранения данных
    
    stack = (char*)malloc(STACK_SIZE); // Выделение памяти под стек
    stackTop = stack + STACK_SIZE; // Установка указателя на вершину стека

    printf("parent start\n"); // Вывод сообщения о начале работы родительского процесса

    pid_t child_pid = clone(child_proc, stackTop, CLONE_NEWNET | SIGCHLD, NULL); // Создание дочернего процесса
    if(child_pid == -1){ // Проверка на успешное создание дочернего процесса
        printf("Error in clone function: %s\n", strerror(errno)); // Вывод сообщения об ошибке
        return 1; // Завершение программы с ошибкой
    }

    prepare_netns(child_pid); // Подготовка сетевого пространства имен для дочернего процесса

    int mysock = socket(AF_INET, SOCK_DGRAM, 0); // Создание сокета
    if (mysock == -1 ) { // Проверка на успешное создание сокета
        perror("parent: create socket error"); // Вывод сообщения об ошибке
        exit(1); // Завершение программы с ошибкой
    } else {
        printf("parent: create socket OK\n"); // Вывод сообщения об успешном создании сокета
    }

    int optval = 1; // Опция для настройки сокета
    if (-1 == setsockopt(mysock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))){ // Установка опции для сокета
        perror("parent: setsockopt error"); // Вывод сообщения об ошибке
        close(mysock); // Закрытие сокета
        exit(1); // Завершение программы с ошибкой
    } else {
        printf("parent: setsockopt OK\n"); // Вывод сообщения об успешной установке опции
    }

    /* привяжем сокет родителя к порту */
    struct sockaddr_in bindaddr; // Структура для адреса привязки
    bindaddr.sin_family = AF_INET; // Установка типа адреса
    bindaddr.sin_port = htons(8000); // Установка порта
    bindaddr.sin_addr.s_addr = inet_addr("10.1.1.1"); // Установка IP-адреса

    if (-1 == bind(mysock, (struct sockaddr*)&bindaddr, sizeof(bindaddr))){ // Привязка сокета к адресу
        perror("parent: bind socket error"); // Вывод сообщения об ошибке
        close(mysock); // Закрытие сокета
        exit(1); // Завершение программы с ошибкой
    } else {
        printf("parent: bind socket OK\n"); // Вывод сообщения об успешной привязке
    }


/* адрес потомка */
struct sockaddr_in saddr; // Структура для хранения адреса потомка
socklen_t saddrlen; // Переменная для хранения размера адресной структуры
memset(&saddr, 0, sizeof(saddr)); // Обнуление структуры адреса
saddr.sin_family = AF_INET; // Установка типа адреса (IPv4)
saddr.sin_port = htons(8000); // Установка порта (8000) с преобразованием в сетевой порядок байт
saddr.sin_addr.s_addr = inet_addr("10.1.1.2"); // Установка IP-адреса потомка
saddrlen = sizeof(saddr); // Установка размера структуры адреса

char rcvbuf[256]; // Буфер для приема данных
memset(rcvbuf, 0, sizeof(rcvbuf)); // Обнуление буфера приема
saddrlen = sizeof(saddr); // Повторная установка размера структуры адреса
int recvcount = recvfrom(mysock, rcvbuf, sizeof(rcvbuf), 0, (struct sockaddr*)&saddr, &saddrlen); // Прием данных
printf("parent: child: veth1 ip = %s; port = %d\n", inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port)); // Вывод IP-адреса и порта потомка
if (recvcount == -1) {
    perror("parent: recv error"); // Вывод сообщения об ошибке приема
} else if (recvcount == 0) {
    printf("parent: reccount = 0\n"); // Вывод сообщения о нулевом количестве принятых данных
} else {
    printf("parent: recvfrom OK: rcvbuf = %s; recvcount = %d\n", rcvbuf, recvcount); // Вывод успешного приема данных
}

char sndbuf[256]; // Буфер для отправки данных
memset(sndbuf, 0, sizeof(sndbuf)); // Обнуление буфера отправки
int len = sprintf(sndbuf, "hello, child!"); // Формирование сообщения для отправки
int sentcount = sendto(mysock, sndbuf, len, 0, (struct sockaddr*)&saddr, saddrlen); // Отправка данных
if (sentcount == -1) {
    perror("parent: send error"); // Вывод сообщения об ошибке отправки
} else {
    printf("parent: sendto OK: sndbuf = %s; sentcount = %d\n", sndbuf, sentcount); // Вывод успешной отправки данных
}
//system("ip addr show veth0"); // Команда для отображения информации о сетевом интерфейсе 

printf("parent: close mysock = %d\n", close(mysock)); // Закрытие сокета и вывод результата закрытия

int status; // Переменная для хранения статуса завершения дочернего процесса
waitpid(child_pid, &status, 0); // Ожидание завершения дочернего процесса

printf("parent finish\n"); // Вывод сообщения о завершении работы родительского процесса
return 0; // Возврат нуля при успешном завершении программы
}

