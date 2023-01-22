#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// 读取命令的缓存结构
typedef struct {
    char* buffer;   // 记录输入的一行命令
    size_t buffer_length;   // 记录缓冲区的大小
    ssize_t input_length;   // 存放输入的字符串个数
} InputBuffer;

// 元命令执行结果的枚举
typedef enum {
    META_COMMAND_SUCCESS,   // 执行成功
    META_COMMAND_UNRECOGNIZED_COMMAND   // 未定义命令
} MetaCommandResult;

// 预处理（预编译）语句执行结果的枚举
typedef enum {
    PREPARE_SUCCESS,    // 预处理成功
    PREPARE_UNRECOGNIZED_STATEMENT  // 未定义语句
} PrepareResult;

// SQL预处理语句的类型的枚举
typedef enum {
    STATEMENT_INSERT,   // insert开头的插入
    STATEMENT_SELECT    // select开头的查询
} StatementType;

// 表示一条SQL语句信息的结构
typedef struct {
    StatementType type;
} Statement;

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
    ssize_t bytes_read = getline(&(input_buffer->buffer), 
                                 &(input_buffer->buffer_length), stdin);
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

// 执行元命令
MetaCommandResult do_meta_command(InputBuffer* input_buffer) {
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        // 退出命令，直接退出
        exit(EXIT_SUCCESS);
    } else {
        // 返回未标识元命令
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

// 解析命令，将解析结果存到statement中，返回解析结果
PrepareResult prepare_statement(InputBuffer* input_buffer, 
                                Statement* statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        // 以insert开头
        statement->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }
    if (strcmp(input_buffer->buffer, "select") == 0) {
        // 以select开头
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

// 执行sql语句
void execute_statement(Statement* statement) {
    switch (statement->type) {
        case STATEMENT_INSERT:
            printf("This is where we would do an insert.\n");
            break;
        case STATEMENT_SELECT:
            printf("This is where we would do an select.\n");
            break;
    }
}

int main(int argc, char* argv[]) {
    InputBuffer* input_buffer = new_input_buffer();
    // 无限循环，知道系统错误或退出指令
    while (true) {
        print_prompt(); // 打印提示符
        read_input(input_buffer);   // 读取一行输入

        // 修改了此处代码
        // if (strcmp(input_buffer->buffer, ".exit") == 0) {
        //     // 输入了退出指令，释放资源并退出系统
        //     close_input_buffer(input_buffer);
        //     exit(EXIT_SUCCESS);
        // } else {
        //     // 输入了其他的未定义的命令
        //     printf("Unrecognized command '%s' .\n", 
        //             input_buffer->buffer);
        // }
        // 以点开头的是元命令
        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer)) {
                case META_COMMAND_SUCCESS:
                    continue;   // 进入while下一个主循环
                case META_COMMAND_UNRECOGNIZED_COMMAND:
                    printf("Unrecongnized command '%s'\n", 
                            input_buffer->buffer);
                    continue;
            }
        }

        // 解析SQL语句
        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                printf("Unrecognized keyword at start of '%s'.\n", 
                        input_buffer->buffer);
                continue;
        }
        // 执行sql语句
        execute_statement(&statement);
        printf("Executed.\n");
    }
}
