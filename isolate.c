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

static void prepare_procfs();  // Объявление статической функции prepare_procfs, которая подготавливает файловую систему proc.
static void prepare_mntns(char *rootfs); // Объявление статической функции prepare_mntns, которая подготавливает пространство имен mount.

struct params {  // Объявление структуры params, которая содержит параметры для функции cmd_exec.
    int fd[2]; // Массив из двух файловых дескрипторов для работы с каналами.
    char **argv; // Указатель на массив строк, который содержит аргументы командной строки.
};

static void parse_args(int argc, char **argv, struct params *params) // Функция parse_args анализирует аргументы командной строки.
{
#define NEXT_ARG() do { argc--; argv++; } while (0)  // Макрос, который переходит к следующему аргументу командной строки.
    // Skip binary path
    NEXT_ARG(); // Пропуск пути к исполняемому файлу.

    if (argc < 1) { // Если аргументов командной строки меньше одного...
        printf("Nothing to do!\n"); //..вывод сообщения том, что делать нечего..
        exit(0);//Завершение программы  с кодом 0
    }

    params->argv = argv;  // Присваивание указателя на массив строк argv полю argv структуры params.
#undef NEXT_ARG // Удаление макроса NEXT_ARG.
}

#define STACKSIZE (1024*1024) // Определение размера стека для функции cmd_exec.
static char cmd_stack[STACKSIZE]; // Объявление статического массива cmd_stack, который будет использоваться как стек для функции cmd_exec.
 
void await_setup(int pipe) // Функция await_setup ожидает сигнал о завершении настройки от основного процесса.
{
    // We're done once we read something from the pipe.
    char buf[2]; // Объявление буфер для чтения из канала.
    if (read(pipe, buf, 2) != 2) // Если чтение из канала не возвращает 2...
        die("Failed to read from pipe: %m\n"); //..вывод сообщение об ошибке и завершение программы.
}

static int cmd_exec(void *arg) // Функция cmd_exec выполняет команду в новом пространстве имен.
{
    // Kill the cmd process if the isolate process dies.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL)) //// Если не удается установить сигнал смерти для процесса...
        die("cannot PR_SET_PDEATHSIG for child process: %m\n"); //...вывод сообщения об ошибке и завершение программы.

    struct params *params = (struct params*) arg; // Приведение указателя arg к типу struct params* и присваивание его переменной params.
    // Wait for 'setup done' signal from the main process.
    await_setup(params->fd[0]);  // Ожидание сигнала о завершении настройки от основного процесса.

    prepare_mntns("rootfs");  // Подготавливание пространство имен mount с корневой файловой системой "rootfs".

    // Assuming, 0 in the current namespace maps to
    // a non-privileged UID in the parent namespace,
    // drop superuser privileges if any by enforcing
    // the exec'ed process runs with UID 0.
    if (setgid(0) == -1) // Если не удается установить идентификатор группы на 0...
        die("Failed to setgid: %m\n");  //...вывод сообщения об ошибке и завершение программы.
    if (setuid(0) == -1)  //Если не удается установить идентификатор пользователя на 0...
        die("Failed to setuid: %m\n"); //...вывод сообщения об ошибке и завершение программы.


    char **argv = params->argv; // Присваивание полю argv структуры params переменной argv.
    char *cmd = argv[0]; // Присваивание первого элемента массива argv переменной cmd.
    printf("===========%s============\n", cmd);  // Вывод имени команды.

    if (execvp(cmd, argv) == -1) // // Если не удается выполнить команду...
        die("Failed to exec %s: %m\n", cmd); //...вывод сообщения об ошибке и завершение программы.

    die("¯\\_(ツ)_/¯"); // Вывод сообщения об ошибке и завершение программы.
    return 1; // Возврат 1
}

static void write_file(char path[100], char line[100]) // Функция write_file записывает строку в файл.
{
    FILE *f = fopen(path, "w"); // Открытия файла по пути path для записи.

    if (f == NULL) {  // Если файл не удалось открыть...
        die("Failed to open file %s: %m\n", path); // ...вывод сообщения об ошибке и завершение программы.
    }

    if (fwrite(line, 1, strlen(line), f) < 0) { // Если не удается записать строку в файл...
        die("Failed to write to file %s:\n", path); //...вывод сообщения об ошибке и завершение программы.
    }

    if (fclose(f) != 0) { // Если не удается закрыть файл...
        die("Failed to close file %s: %m\n", path); // ...вывод сообщения об ошибке и завершение программы.
    }
}

static void prepare_userns(int pid) // Функция prepare_userns подготавливает пространство имен пользователя.
{
    char path[100]; // Объявление массива символов для хранения пути к файлу.
    char line[100]; // Объявляение массива символов для хранения строки, которую нужно записать в файл.

    int uid = 1000;  // Объявления идентификатора пользователя, который будет использоваться в пространстве имен пользователя.

    sprintf(path, "/proc/%d/uid_map", pid); // Форматирование строки и сохраняет ее в массиве path.
    sprintf(line, "0 %d 1\n", uid); // Форматирование строки и сохранение ее в массиве line.
    write_file(path, line); // Запись строки из массива line в файл по пути, указанному в массиве path.

    sprintf(path, "/proc/%d/setgroups", pid); // Форматирование строки и сохранение ее в массиве path.
    sprintf(line, "deny"); // Сохранение строки "deny" в массиве line.
    write_file(path, line);// Запись строки из массива line в файл по пути, указанному в массиве path.

    sprintf(path, "/proc/%d/gid_map", pid);  // Форматирование строки и сохранение ее в массиве path.
    sprintf(line, "0 %d 1\n", uid); // Форматирование строки и сохранение ее в массиве line.
    write_file(path, line);  // Запись строки из массива line в файл по пути, указанному в массиве path.
}

static void prepare_mntns(char *rootfs)  // Функция prepare_mntns подготавливает пространство имен mount.
{
    const char *mnt = rootfs; // Присваивание указатель на корневую файловую систему переменной mnt.

    if (mount(rootfs, mnt, "ext4", MS_BIND, "")) // Если не удается смонтировать корневую файловую систему...
        die("Failed to mount %s at %s: %m\n", rootfs, mnt); //...вывод сообщения об ошибке и завершение программы.

    if (chdir(mnt)) // Если не удается изменить текущий рабочий каталог на mnt...
        die("Failed to chdir to rootfs mounted at %s: %m\n", mnt); //...вывод сообщения об ошибке и завершение программы.


    const char *put_old = ".put_old";  // Объявление указателя на строку "put_old".
    if (mkdir(put_old, 0777) && errno != EEXIST) // Если не удается создать каталог "put_old" и он уже не существует
        die("Failed to mkdir put_old %s: %m\n", put_old); //...вывод сообщения об ошибке и завершение программы.

    if (syscall(SYS_pivot_root, ".", put_old)) // Если не удается изменить корневую файловую систему
        die("Failed to pivot_root from %s to %s: %m\n", rootfs, put_old); //...вывод сообщения об ошибке и завершение программы.

    if (chdir("/")) // Если не удается изменить текущий рабочий каталог на "/"...
        die("Failed to chdir to new root: %m\n");//...вывод сообщения об ошибке и завершение программы.

    prepare_procfs(); // Подготовка файловой системы proc.

    if (umount2(put_old, MNT_DETACH))  // Если не удается отмонтировать "put_old"...
        die("Failed to umount put_old %s: %m\n", put_old);//...вывод сообщения об ошибке и завершение программы.
}

static void prepare_procfs() // Функция prepare_procfs подготавливает файловую систему proc.
{
    if (mkdir("/proc", 0555) && errno != EEXIST) // Если не удается создать каталог "/proc" и он уже не существует...
        die("Failed to mkdir /proc: %m\n");//...вывод сообщения об ошибке и завершение программы.

    if (mount("proc", "/proc", "proc", 0, ""))  // Если не удается смонтировать файловую систему proc...
        die("Failed to mount proc: %m\n");//...вывод сообщения об ошибке и завершение программы.
}

static void prepare_netns(int cmd_pid) // Функция prepare_netns подготавливает пространство имен сети.
{
    char *veth = "veth0"; // Объявление указателя на строку "veth0", который будет использоваться как имя виртуального Ethernet-интерфейса.
    char *vpeer = "veth1"; // Объявление указателя на строку "veth1", который будет использоваться как имя парного виртуального Ethernet-интерфейса.
    char *veth_addr = "10.1.1.1"; // Объявление указателя на строку "10.1.1.1", который будет использоваться как IP-адрес для veth.
    char *vpeer_addr = "10.1.1.2"; // Объявление указателя на строку "10.1.1.2", который будет использоваться как IP-адрес для vpeer.
    char *netmask = "255.255.255.0"; // Объявление указателя на строку "255.255.255.0", который будет использоваться как маска подсети.

    int sock_fd = create_socket( // Создание сокета для работы с протоколом NETLINK_ROUTE.
            PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);

    create_veth(sock_fd, veth, vpeer); // Создание пары виртуальных Ethernet-интерфейсов veth и vpeer.

    if_up(veth, veth_addr, netmask); // Включение интерфейса veth и установка его IP-адреса и маски подсети.

    int mynetns = get_netns_fd(getpid()); // Получение файлового дескриптора пространства имен сети текущего процесса.
    int child_netns = get_netns_fd(cmd_pid); // Получение файлового дескриптора пространства имен сети процесса с идентификатором cmd_pid.

    move_if_to_pid_netns(sock_fd, vpeer, child_netns); // Перемещение интерфейса vpeer в пространство имен сети процесса с идентификатором cmd_pid.

    if (setns(child_netns, CLONE_NEWNET)) // Если не удается переключиться на пространство имен сети процесса с идентификатором cmd_pid...
        die("Failed to setns for command at pid %d: %m\n", cmd_pid); // ...вывод сообщеня об ошибке и завершение программы.

    if_up(vpeer, vpeer_addr, netmask); // Включение интерфейса vpeer и установка его IP-адреса и маски подсети.

    if (setns(mynetns, CLONE_NEWNET)) // Если не удается восстановить предыдущее пространство имен сети...
        die("Failed to restore previous net namespace: %m\n"); // ...вывод сообщения об ошибке и завершение программы.

    close(sock_fd); // Закрытие сокета
}

int main(int argc, char **argv) // Главная функция программы.
{

    struct params params; // Объявление структуры params.
    memset(&params, 0, sizeof(struct params)); // Инициализация структуры params нулями.

    parse_args(argc, argv, &params); // Анализ аргументов командной строки.

    // Create pipe to communicate between main and command process.
    if (pipe(params.fd) < 0) // Если не удается создать канал...
        die("Failed to create pipe: %m"); // ...вывод сообщения об ошибке и завершение программы.

    // Clone command process.
    int clone_flags = // Объявление флагов для функции clone.
            // if the command process exits, it leaves an exit status
            // so that we can reap it.
            SIGCHLD |
            CLONE_NEWUTS | CLONE_NEWUSER |
            CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET;
    int cmd_pid = clone( // Создание нового процесса, который будет выполнять функцию cmd_exec.
        cmd_exec, cmd_stack + STACKSIZE, clone_flags, &params);

    if (cmd_pid < 0) // Если не удается создать новый процесс...
        die("Failed to clone: %m\n"); // ...вывод сообщения об ошибке и завершение программы.

    // Get the writable end of the pipe.
    int pipe = params.fd[1]; // Получение записываемого конца канала.

    prepare_userns(cmd_pid); // Подготавка пространства имен пользователя для процесса с идентификатором cmd_pid.
    prepare_netns(cmd_pid); // Подготавка пространства имен сети для процесса с идентификатором cmd_pid.

    // Signal to the command process we're done with setup.
    if (write(pipe, "OK", 2) != 2) // Если не удается записать "OK" в канал...
        die("Failed to write to pipe: %m"); // ...вывод сообщения об ошибке и завершение программы.
    if (close(pipe)) // Если не удается закрыть канал...
        die("Failed to close pipe: %m"); // ...вывод сообщения об ошибке и завершение программы.

    if (waitpid(cmd_pid, NULL, 0) == -1) // Если не удается дождаться завершения процесса с идентификатором cmd_pid...
        die("Failed to wait pid %d: %m\n", cmd_pid); // ...вывод сообщения об ошибке и завершение программы.

    return 0; // Возвращение кода успешного завершения программы
}

