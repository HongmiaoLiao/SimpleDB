#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// 读取命令的缓存结构
typedef struct {
    char* buffer;   // 记录输入的一行命令
    size_t buffer_length;   // 记录缓冲区的大小
    ssize_t input_length;   // 存放输入的字符串个数
} InputBuffer;

// SQL语句执行结果的枚举
typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;

// 元命令执行结果的枚举
typedef enum {
    META_COMMAND_SUCCESS,   // 执行成功
    META_COMMAND_UNRECOGNIZED_COMMAND   // 未定义命令
} MetaCommandResult;

// 预处理（预编译）语句执行结果的枚举
typedef enum {
    PREPARE_SUCCESS,    // 预处理成功
    PREPARE_SYNTAX_ERROR,   // 语句存在语法错误
    PREPARE_UNRECOGNIZED_STATEMENT  // 未定义语句
} PrepareResult;

// SQL预处理语句的类型的枚举
typedef enum {
    STATEMENT_INSERT,   // insert开头的插入
    STATEMENT_SELECT    // select开头的查询
} StatementType;

#define COLUMN_USERNAME_SIZE 32 // 用户名长度
#define COLUMN_EMAIL_SIZE 255   // 邮箱长度

// 数据库中，固定的一行的结构体
typedef struct {
    uint32_t id;    // 用户id
    char username[COLUMN_USERNAME_SIZE];    // 存放用户名的字符数组
    char email[COLUMN_EMAIL_SIZE];  // 存放邮箱的字符数组
} Row;

// 表示一条SQL语句信息的结构
typedef struct {
    StatementType type; // 语句类型。是insert还是select等
    Row row_to_insert;  // 只用于插入语句
} Statement;

// 常量0是空指针，将空指针强转为Struct类型指针，然后这个Struct类型指针就可以访问成员
#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)

const uint32_t ID_SIZE = size_of_attribute(Row, id);    // id占用内存大小
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);  // 用户名占用内存大小  
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);  // 邮箱占用内存大小
const uint32_t ID_OFFSET = 0;   // 初始位置内存偏移量，也就是在行指针指向的开头
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;   // 用户名位置偏移量
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;  // 邮箱位置偏移量
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE; // 一行最小占用内存空间

#define TABLE_MAX_PAGES 100         // 定义一个表最多100页
const uint32_t PAGE_SIZE = 4096;    // 定义一页大小为4096字节
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;    // 每页能放的行数
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;    //一个表最多有多少行

// 一个表结构的结构体
typedef struct {
    uint32_t num_rows;  // 记录当前的行数
    void* pages[TABLE_MAX_PAGES];   // 表中的每一页
} Table;

// 打印出一行
void print_row(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

// 序列化一行，将结构体进行压缩，使一行在内存中是紧凑的结构
// source是行结构体的指针，destination是要存放的紧凑内存的位置开头
void serialize_row(Row* source, void* destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

// 将内存中紧凑的一行内容反序列化为结构体
// source是存放一行的内存紧凑结构的开始位置，destination是存放一行的结构体
void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

// 找到要插入到表中的位置
void* row_slot(Table* table, uint32_t row_num) {
    uint32_t page_num = row_num / ROWS_PER_PAGE;    // 第row_num行在第page_num页
    void* page = table->pages[page_num];    //指向第page_num页的指针
    if (page == NULL) {
        // 如果该页还未分配，说明当前行是新一页的第一行
        // 只有在我们试图访问页时，才分配内存

        page = table->pages[page_num] = malloc(PAGE_SIZE);
    }
    uint32_t row_offset = row_num % ROWS_PER_PAGE;  // 位于当前页的第几行
    uint32_t byte_offset = row_offset * ROW_SIZE;   // 当前行在本页的内存偏移量
    return page + byte_offset;  // 返回要插入行的位置的指针
}

// 新建并初始化一个表，返回这个表的指针
Table* new_table() {
    Table* table = (Table*)malloc(sizeof(Table));
    table->num_rows = 0;    // 初始有0行
    // 将指向每一页的指针置为空
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; ++i) {
        table->pages[i] = NULL;
    }
    return table;
}

// 释放表资源
void free_table(Table* table) {
    // 先释放每一页的资源,从头开始释放直到空页或到达最后一页
    for (int i = 0; table->pages[i] && i < TABLE_MAX_PAGES; ++i) {
        free(table->pages[i]);
    }
    // 再释放整个表
    free(table);
}

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
    // 需要先释放缓存，再释放整个结构体
    free(input_buffer->buffer);
    free(input_buffer);
}

// 执行元命令
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        // 退出命令，关闭输入缓存区，释放整张表
        close_input_buffer(input_buffer);
        free_table(table);
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
        // 将insert的内容存放到一个行结构体中
        int args_assigned = sscanf(input_buffer->buffer, "insert %d %s %s", 
                                    &(statement->row_to_insert.id),
                                    statement->row_to_insert.username,
                                    statement->row_to_insert.email);
        if (args_assigned < 3) {
            // 输入的内容不足，目前需要id, name,eamil
            return PREPARE_SYNTAX_ERROR;
        }
        return PREPARE_SUCCESS;
    }
    if (strcmp(input_buffer->buffer, "select") == 0) {
        // 以select开头
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

// 执行插入语句，返回是否执行结果的枚举
// void execute_statement(Statement* statement) {
ExecuteResult execute_insert(Statement* statement, Table* table) {
    // 表已经满了
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }
    // 一行的结构体存放在statement中，先取出
    Row* row_to_insert = &(statement->row_to_insert);

    // 序列化行结构体，并插入到表中
    serialize_row(row_to_insert, row_slot(table, table->num_rows));
    table->num_rows += 1;   // 表中的行数+1

    return EXECUTE_SUCCESS;
}

// 执行查询语句，查询一个表中的全部内容，并打印到控制台
ExecuteResult execute_select(Statement* statement, Table* table) {
    Row row;
    for (uint32_t i = 0; i < table->num_rows; ++i) {
        deserialize_row(row_slot(table, i), &row);
        print_row(&row);
    }
    return EXECUTE_SUCCESS;
}

// 统一判断要执行语句，并将其执行
ExecuteResult execute_statement(Statement* statement, Table* table) {
    switch (statement->type) {
        case STATEMENT_INSERT:
            return execute_insert(statement, table);
        case STATEMENT_SELECT:
            return execute_select(statement, table);
    }
}

int main(int argc, char* argv[]) {
    Table* table = new_table();
    InputBuffer* input_buffer = new_input_buffer();
    // 无限循环，知道系统错误或退出指令
    while (true) {
        print_prompt(); // 打印提示符
        read_input(input_buffer);   // 读取一行输入
        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case META_COMMAND_SUCCESS:
                    continue;   // 进入while下一个主循环
                case META_COMMAND_UNRECOGNIZED_COMMAND:
                    printf("Unrecongnized command '%s'\n", 
                            input_buffer->buffer);
                    continue;
            }
        }

        // 解析SQL语句，解析结果放到statement结构体中。
        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case PREPARE_SUCCESS:
                break;
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Could not parse statement. \n");
                continue;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                printf("Unrecognized keyword at start of '%s'.\n", 
                        input_buffer->buffer);
                continue;
        }
        // 执行sql语句
        // execute_statement(&statement);
        // printf("Executed.\n");
        switch (execute_statement(&statement, table)) {
            case EXECUTE_SUCCESS:
                // printf("num_rows: %d\n", table->num_rows);
                // printf("%d\n", table->num_rows);
                printf("Executed.\n");
                break;
            case EXECUTE_TABLE_FULL:
                printf("Error: Table full.\n");
                break;
        }
    }
}
