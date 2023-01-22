#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// 读取命令的缓存结构
typedef struct {
    char* buffer;   // 记录输入的一行命令
    size_t buffer_length;   // 记录缓冲区的大小
    ssize_t input_length;   // 存放输入的字符串个数
    // (ssize_t 是在linux环境下的一个类型unsign size_t， windows中vscode会有错误提示)
} InputBuffer;

// 新建一个缓存结构，初始化并返回
InputBuffer* new_input_buffer() {
    InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

// 打印提示
void print_prompt() {
    printf("db > ");
}

// 读取输入
void read_input(InputBuffer* input_buffer) {
    ssize_t bytes_read= getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read <= 0) {
        // 读取字符失败
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    // 忽略末尾换行符的长度
    input_buffer->input_length = bytes_read - 1;
    // 将原本换行符的位置变为0
    input_buffer->buffer[bytes_read - 1] = 0;
}

// 关闭读取命令的缓存，释放资源
void close_input_buffer(InputBuffer* input_buffer) {
    // 需要先是否缓存，在释放整个结构体
    free(input_buffer->buffer);
    free(input_buffer);
}

int main(int argc, char* argv[]) {
    InputBuffer* input_buffer = new_input_buffer();
    // 无限循环，知道系统错误或退出指令
    while (true) {
        print_prompt(); // 打印提示符
        read_input(input_buffer);   // 读取一行输入

        if (strcmp(input_buffer->buffer, ".exit") == 0) {
            // 输入了退出指令，释放资源并退出系统
            close_input_buffer(input_buffer);
            exit(EXIT_SUCCESS);
        } else {
            // 输入了其他的未定义的命令
            printf("Unrecognized command '%s' .\n", input_buffer->buffer);
        }
    }
}
