#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

// 读取命令的缓存结构
typedef struct {
    char* buffer;   // 记录输入的一行命令
    size_t buffer_length;   // 记录缓冲区的大小
    ssize_t input_length;   // 存放输入的字符串个数
} InputBuffer;

// SQL语句执行结果的枚举
typedef enum {
    EXECUTE_SUCCESS,    // 执行成功
    EXECUTE_TABLE_FULL, // 表以满
    EXECUTE_DUPLICATE_KEY   // 有重复键
} ExecuteResult;

// 元命令执行结果的枚举
typedef enum {
    META_COMMAND_SUCCESS,   // 执行成功
    META_COMMAND_UNRECOGNIZED_COMMAND   // 未定义命令
} MetaCommandResult;

// 预处理（预编译）语句执行结果的枚举
typedef enum {
    PREPARE_SUCCESS,    // 预处理成功
    PREPARE_NEGATIVE_ID,    // ID为负
    PREPARE_STRING_TOO_LONG,    // 输入的字符过长（姓名、邮箱）
    PREPARE_SYNTAX_ERROR,   // 语句存在语法错误
    PREPARE_UNRECOGNIZED_STATEMENT  // 未定义语句
} PrepareResult;

// SQL预处理语句的类型的枚举
typedef enum {
    STATEMENT_INSERT,   // insert开头的插入
    STATEMENT_SELECT    // select开头的查询
} StatementType;

const uint32_t COLUMN_USERNAME_SIZE = 32; // 用户名长度
const uint32_t COLUMN_EMAIL_SIZE = 255;   // 邮箱长度

// 数据库中，固定的一行的结构体
typedef struct {
    uint32_t id;    // 用户id
    char username[COLUMN_USERNAME_SIZE + 1];    // 存放用户名的字符数组
    char email[COLUMN_EMAIL_SIZE + 1];  // 存放邮箱的字符数组
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

const uint32_t TABLE_MAX_PAGES = 100;         // 定义一个表最多100页
const uint32_t PAGE_SIZE = 4096;    // 定义一页大小为4096字节
// const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;    // 每页能放的行数
// const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;    //一个表最多有多少行

// 页管理器结构
typedef struct {
    int file_descriptor;    // 数据库持久化文件的文件描述符
    uint32_t file_length;   // 数据库文件的总长度
    uint32_t num_pages;     // 总的页数
    void* pages[TABLE_MAX_PAGES];   // 表中的每一页
} Pager;

// 一个表结构的结构体
typedef struct {
    // uint32_t num_rows;  // 记录当前的行数
    uint32_t root_page_num; // 根节点的页数
    Pager* pager;   // 由表管理页管理器
} Table;

// 游标的结构
typedef struct {
    Table* table;   // 表的引用
    // uint32_t row_num;   // 游标所指向的行号
    uint32_t page_num;  // 指向的页码
    uint32_t cell_num;  // 页内的单元位置
    bool end_of_table;  // 指示最后一个元素的下一个位置
} Cursor;

// 表示节点类型的枚举
typedef enum {
    NODE_INTERNAL,  // 内部节点
    NODE_LEAF   // 叶节点
} NodeType;

// 通用结点头部布局
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);    // 节点类型大小
const uint32_t NODE_TYPE_OFFSET = 0;    // 节点类型偏移量
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);  // 根节点
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE; // 根结点偏移量
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);  // 父结点指针大小
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;   // 父节点指针偏移
const uint8_t COMMON_NODE_HEADER_SIZE = 
            NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;    //节点共同头部的大小

// 叶节点头部布局
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);     // 记录叶节点中单元个数空间的大小
// 紧跟通用头部部分
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
// const uint32_t LEAF_NODE_HEADER_SIZE = 
//             COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE; 
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t); // 叶节点表示兄弟节点的位置大小
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET = 
        LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;  // 叶节点表示兄弟位置的偏移
const uint32_t LEAF_NODE_HEADER_SIZE = 
        COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;  // 叶节点头部大小

// 页节点主体布局
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);   // 一个结点的键的大小
const uint32_t LEAF_NODE_KEY_OFFSET = 0;    // 键的偏移
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE; // 叶节点值就是一行，大小和行大小一样
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;  // 值的偏移量
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE; // 一个单元有键值构成，大小就是键值大小之和
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;   // 剩下的用于存储的空间
const uint32_t LEAF_NODE_MAX_CELLS = LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;   // 剩下空间能存放的单元个数

// 获取一个节点有多少个单元
uint32_t* leaf_node_num_cells(void* node) {
    return (uint32_t*) (node + LEAF_NODE_NUM_CELLS_OFFSET);
}

// 获取指向一个单元的数据的指针
void* leaf_node_cell(void* node, uint32_t cell_num) {
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

// 获取一个单元的键
uint32_t* leaf_node_key(void* node, uint32_t cell_num) {
    return (uint32_t*) leaf_node_cell(node, cell_num);
}

// 获取一个单元的值
void* leaf_node_value(void* node, uint32_t cell_num) {
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

// 获取一个结点的类型
NodeType get_node_type(void* node) {
    uint8_t value = *((uint8_t*)(node + NODE_TYPE_OFFSET));
    return (NodeType)value;
}

// 设置一个结点的类型
void set_node_type(void* node, NodeType type) {
    uint8_t value = type;
    *((uint8_t*)(node + NODE_TYPE_OFFSET)) = value;
}

// 获取节点是否为根节点
bool is_node_root(void* node) {
    uint8_t value = *((uint8_t*)(node + IS_ROOT_OFFSET));
    return (bool)value;
}

// 设置一个结点类型为根节点
void set_node_root(void* node, bool is_root) {
    uint8_t value = is_root;
    *((uint8_t*)(node + IS_ROOT_OFFSET)) = value;
}

// 获取叶节点的下一个兄弟节点
uint32_t* leaf_node_next_leaf(void* node) {
    return (uint32_t*)(node + LEAF_NODE_NEXT_LEAF_OFFSET);
}

const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2; // 左半部分节点个数
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;    // 右半部分结点个数

/*
内部节点的头部布局
*/
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);  // 内部节点键个数所占空间
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE; // 内部节点键的个数所在位置
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);   // 右子结点页码大小
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET = INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;  // 右子结点页码位置
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE;  // 内部结点的头部大小

/*
内部节点主体布局
*/
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);   // 内部节点键的大小
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t); // 内部节点子结点指针大小
const uint32_t INTERNAL_NODE_CELL_SIZE = INTERNAL_NODE_KEY_SIZE + INTERNAL_NODE_CHILD_SIZE;     // 内部节点一个单元格大小
const uint32_t INTERNAL_NODE_MAX_CELLS = 3; // 一个内部节点最多有几个单元格。 将这个值保持较小，便于测试

// 返回指向内部节点键的数量的指针
uint32_t* internal_node_num_keys(void* node) {
    return (uint32_t*)(node + INTERNAL_NODE_NUM_KEYS_OFFSET);
}

// 返回指向内部节点右子结点位置的指针
uint32_t* internal_node_right_child(void* node) {
    return (uint32_t*)(node + INTERNAL_NODE_RIGHT_CHILD_OFFSET);
}

// 返回内部节点指定第几个的单元格子结点位置的指针
uint32_t* internal_node_cell(void* node, uint32_t cell_num) {
    return (uint32_t*)(node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE);
}

// 返回内部节点指定第几个的孩子节点位置指针
uint32_t* internal_node_child(void* node, uint32_t child_num) {
    uint32_t num_keys = *internal_node_num_keys(node);
    if (child_num > num_keys) {
        // 访问个数超过已有个数
        printf("Tried to access child_num %d > num_keys %d\n", child_num, num_keys);
        exit(EXIT_FAILURE);
    } else if (child_num == num_keys) {
        // 最后一个是右节点指针
        return internal_node_right_child(node);
    } else{
        // 访问子结点位置指针
        return internal_node_cell(node, child_num);
    }
}

// 返回指向内部节点子结点的键的指针
uint32_t* internal_node_key(void* node, uint32_t key_num) {
    return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

// 获得一个节点的最大键
uint32_t get_node_max_key(void* node) {
    switch (get_node_type(node)) {
        case NODE_INTERNAL:
        // 对于内部节点，最大键总是它的最右边的键
            return *internal_node_key(node, *internal_node_num_keys(node)-1);
        case NODE_LEAF:
        // 对于叶节点，最大键是最大索引处的键
            return *leaf_node_key(node, *leaf_node_num_cells(node)-1);
    }
}

// 初始化叶结点，即设置一个节点的单元个数初始为0
void initialize_leaf_node(void* node) {
    set_node_type(node, NODE_LEAF);
    set_node_root(node, false); // 初始节点不是根节点
    *leaf_node_num_cells(node) = 0; 
    *leaf_node_next_leaf(node) = 0; // 0 表示兄弟节点
}

// 初始化内部节点
void initialize_internal_node(void* node) {
    set_node_type(node, NODE_INTERNAL);
    set_node_root(node, false);
    *internal_node_num_keys(node) = 0;
}

// 获取一个结点的父母节点的位置
uint32_t* node_parent(void* node) {
    return (uint32_t*)(node + PARENT_POINTER_OFFSET);
}

// 10>

// 打印表相关的常量
void print_constants() {
    printf("ROW_SIZE: %d\n", ROW_SIZE);
    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

// 打印叶子节点的结构
// void print_leaf_node(void* node) {
//     uint32_t num_cells = *leaf_node_num_cells(node);
//     printf("leaf (size %d)\n", num_cells);
//     for (uint32_t i = 0; i < num_cells; ++i) {
//         uint32_t key = *leaf_node_key(node, i);
//         printf("  - %d : %d\n", i, key);
//     }
// }

// 根据层级，打印缩进，一层两个空格
void indent(uint32_t level) {
    for (uint32_t i = 0; i < level; ++i) {
        printf("  ");
    }
}

// 使用页管理器，获取一页
void* get_page(Pager* pager, uint32_t page_num) {
    if (page_num > TABLE_MAX_PAGES) {
        // 要获取页数超过最大页数，打印提示信息并退出
        printf("Tried to fetch page number out of bounds. %d > %d\n",
                page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    // 并不是一次性读取整个文件，而是在查询时如果不在内存才分配内存读取
    if (pager->pages[page_num] == NULL) {
        // 缓存丢失。分配内存并从文件载入。
        void* page = malloc(PAGE_SIZE);
        uint32_t num_pages = pager->file_length / PAGE_SIZE;

        // 我们可以在文件的末尾保存部分页面，就是最后可能不满的那一页
        if (pager->file_length % PAGE_SIZE) {
            num_pages += 1;
        }

        if (page_num <= num_pages) {
            // 要获取的页不超过目前已有的总页数
            // 使文件偏移量移动到要读取的页的开头
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);  
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1) {
                // 读取失败
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }
        // 使页管理器管理读取的页
        pager->pages[page_num] = page;

        // 页管理器跟踪页数
        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }
    // 返回当前读取的页
    return pager->pages[page_num];
}

// 可视化一棵某个节点作为根的数
void print_tree(Pager* pager, uint32_t page_num, uint32_t indentation_level) {
    void* node = get_page(pager, page_num); // 根据页码从也管理器获得结点
    uint32_t num_keys;
    uint32_t child;
    // 根据节点类型执行打印操作
    switch (get_node_type(node)) {
        case NODE_LEAF:
            // 叶节点
            num_keys = *leaf_node_num_cells(node);
            indent(indentation_level);
            printf("- leaf (size %d)\n", num_keys);
            // 打印叶节点所有键
            for (uint32_t i = 0; i < num_keys; ++i) {
                indent(indentation_level + 1);
                printf("- %d\n", *leaf_node_key(node, i));
            }
            break;
        case NODE_INTERNAL:
            // 内部节点
            num_keys = *internal_node_num_keys(node);
            indent(indentation_level);
            printf("- internal (size %d)\n", num_keys);
            for (uint32_t i = 0; i < num_keys; ++i) {
                // 递归打印内部节点的子节点
                child = *internal_node_child(node, i);
                print_tree(pager, child, indentation_level+1);
                // 打印内部节点的键
                indent(indentation_level + 1);
                printf("- key %d\n", *internal_node_key(node, i));
            }
            // 递归打印右指针指向的子节点
            child = *internal_node_right_child(node);
            print_tree(pager, child, indentation_level + 1);
            break;
    }
}

// // 新建一个游标，指向表尾位置，即下一个要插入的位置
// Cursor* table_end(Table* table) {
//     Cursor* cursor = (Cursor*) malloc(sizeof(Cursor));
//     cursor->table = table;
//     // 指向最后一行之后，注意，存放位置是从0开始，行数是从1开始
//     // 所以行数就是最后插入位置的下标
//     // cursor->row_num = table->num_rows;
//     cursor->page_num = table->root_page_num;

//     void* root_node = get_page(table->pager, table->root_page_num);
//     uint32_t num_cells = *leaf_node_num_cells(root_node);
//     cursor->cell_num = num_cells;  
//     cursor->end_of_table = true;    // 标识指向最后一行

//     return cursor;
// }

// 在指定页上使用二分搜索查找键对应的节点
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    // 获取页管理器
    void* node = get_page(table->pager, page_num);
    // 当前页一共有多少多个单元
    uint32_t num_cells = *leaf_node_num_cells(node); 
    // 新建一个游标
    Cursor* cursor = (Cursor*)malloc(sizeof(Cursor));
    cursor->table = table;  // 设置游标和表对应
    cursor->page_num = page_num;    // 游标指向的页

    // 二分搜索
    uint32_t min_index = 0; // 左边界
    uint32_t one_past_max_index = num_cells;    // 右边界，即该叶节点数
    while (one_past_max_index != min_index) {
        uint32_t index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_index = *leaf_node_key(node, index);
        if (key == key_at_index) {
            // 找到对应位置
            cursor->cell_num = index;
            return cursor;
        }
        if (key < key_at_index) {
            // 右边界收缩
            one_past_max_index = index;
        } else {
            // 左边界收缩
            min_index = index + 1;
        }
    }
    // 游标指向要插入的位置
    cursor->cell_num = min_index;
    return cursor;
}

/*
在我们开始回收释放的页之前，新页会一直添加到数据库文件后面
*/
uint32_t get_unused_page_num(Pager* pager) {
    return pager->num_pages;
}

void create_new_root(Table* table, uint32_t right_child_page_num) {
    /*
    处理根节点分裂
    旧的根节点复制到新页，变为左子结点
    参数传入了右子结点的地址
    重新初始化根的页以包含新的根节点
    新的根节点指向两个子结点。
    */
   void* root = get_page(table->pager, table->root_page_num);
   void* right_child = get_page(table->pager, right_child_page_num);
   uint32_t left_child_page_num = get_unused_page_num(table->pager);
   void* left_child = get_page(table->pager, left_child_page_num);

   /* 左子结点的数据由旧的根节点复制过来*/
   memcpy(left_child, root, PAGE_SIZE);
   set_node_root(left_child, false);

   /*根节点是由一个键和两个子结点的新内部结点*/
   initialize_internal_node(root);
   set_node_root(root, true);
   *internal_node_num_keys(root) = 1;
   *internal_node_child(root, 0) = left_child_page_num;
   uint32_t left_child_max_key = get_node_max_key(left_child);
   *internal_node_key(root, 0) = left_child_max_key;
   *internal_node_right_child(root) = right_child_page_num;
   *node_parent(left_child) = table->root_page_num;
   *node_parent(right_child) = table->root_page_num;
}

// 对内部节点使用二分查找获取指向指定键的游标
// Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
//     void* node = get_page(table->pager, page_num);
uint32_t internal_node_find_child(void* node, uint32_t key) {
    /*
    返回包含给定键的孩子的索引
    */

    uint32_t num_keys = *internal_node_num_keys(node);

    /* 二分查找子索引*/
    uint32_t min_index = 0;
    uint32_t max_index = num_keys;  /* 子指针比键多一个*/

    while (min_index != max_index) {
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_to_right = *internal_node_key(node, index);
        if (key_to_right >= key) {
            max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    // 找到正确节点后，调用相应的搜索函数
    // uint32_t child_num = *internal_node_child(node, min_index);
    return min_index;
}

// 递归查找指向给定键的叶节点的游标
Cursor* internal_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = get_page(table->pager, page_num);

    uint32_t child_index = internal_node_find_child(node, key);
    uint32_t child_num = *internal_node_child(node, child_index);
    void* child = get_page(table->pager, child_num);
    switch (get_node_type(child)) {
        case NODE_LEAF:
            return leaf_node_find(table, child_num, key);
        case NODE_INTERNAL:
            return internal_node_find(table, child_num, key);
    }
}

/*
返回给定键的位置。
如果键不存在，返回它应该被插入的位置
*/
Cursor* table_find(Table* table, uint32_t key) {
    uint32_t root_page_num = table->root_page_num;
    
    void* root_node = get_page(table->pager, root_page_num);
    // 根据节点类型进行查找
    if (get_node_type(root_node) == NODE_LEAF) {
        // 叶节点中，找到要插入位置
        return leaf_node_find(table, root_page_num, key);
    } else {
        // printf("Need to implement searching an internal node\n");
        // exit(EXIT_FAILURE);
        return internal_node_find(table, root_page_num, key);
    }
}

// 新建一个游标，指向表头位置
// Cursor* table_start(Table* table) {
//     Cursor* cursor = (Cursor*) malloc(sizeof(Cursor));
//     cursor->table = table;
//     // cursor->row_num = 0;
//     // // 若表为空，开始位置也是结束位置
//     // cursor->end_of_table = (table->num_rows == 0);
//     cursor->page_num = table->root_page_num;
//     cursor->cell_num = 0;

//     void* root_node = get_page(table->pager, table->root_page_num);
//     uint32_t num_cells = *leaf_node_num_cells(root_node);
//     cursor->end_of_table = (num_cells == 0);    // 是否是结尾

//     return cursor;
// }

// 返回表头位置的游标
Cursor* table_start(Table* table) {
    Cursor* cursor = table_find(table, 0);
    void* node = get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);
}

// 使游标后移一个位置
void cursor_advance(Cursor* cursor) {
    // 获取节点位置
    uint32_t page_num = cursor->page_num;
    void* node = get_page(cursor->table->pager, page_num);

    // 游标往后移动一个位置
    cursor->cell_num += 1;
    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
        // cursor->end_of_table = true;
        // 移动到下一个叶节点
        uint32_t next_page_num = *leaf_node_next_leaf(node);
        if (next_page_num == 0) {
            // 这是最右边的叶节点
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_page_num;
            cursor->cell_num = 0;
        }
    }
}

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
    // 去除不影响的乱码垃圾，用字符串复制
    // strncpy((char*) destination + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    // strncpy((char*) destination + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

// 将内存中紧凑的一行内容反序列化为结构体
// source是存放一行的内存紧凑结构的开始位置，destination是存放一行的结构体
void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

// void* row_slot(Table* table, uint32_t row_num) {
// 根据当前游标的位置，映射到要插入表的内存位置
void* cursor_value(Cursor* cursor) {
    // uint32_t row_num = cursor->row_num; // 从游标中获取所指行数
    // uint32_t page_num = row_num / ROWS_PER_PAGE;    // 第row_num行在第page_num页
    uint32_t page_num = cursor->page_num;
    // void* page = get_page(table->pager, page_num);  
    void* page = get_page(cursor->table->pager, page_num); // 用页管理区来获取页
    // uint32_t row_offset = row_num % ROWS_PER_PAGE;  // 位于当前页的第几行
    // uint32_t byte_offset = row_offset * ROW_SIZE;   // 当前行在本页的内存偏移量
    // return page + byte_offset;  // 返回要插入行的位置的指针
    return leaf_node_value(page, cursor->cell_num);
}

// 初始化页管理器，打开文件并由页管理器管理，返回页管理器
Pager* pager_open(const char* filename) {
    // 打开文件，使用读/写模式，如果文件不存在就创建文件，用户有写权限和读权限
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);
    
    if (fd == -1) {
        // 打开文件失败
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }
    // 文件偏移量设置为文件的大小加上偏移量字节偏移量为0就是返回文件大小
    off_t file_length = lseek(fd, 0, SEEK_END);

    // 初始化页管理器
    Pager* pager =(Pager*) malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    // 每页的指针先置为NULL
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; ++i) {
        pager->pages[i] = NULL;
    }

    return pager;
}

//

// 更新内部节点的键，用新键替换旧的键
void update_internal_node_key(void* node, uint32_t old_key, uint32_t new_key) {
    uint32_t old_child_index = internal_node_find_child(node, old_key);
    *internal_node_key(node, old_child_index) = new_key;
}

void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
    /*
    添加一个新的孩子/键对到对应于子节点的父母节点
    */
    void* parent = get_page(table->pager, parent_page_num);
    void* child = get_page(table->pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(child);
    uint32_t index = internal_node_find_child(parent, child_max_key);

    uint32_t original_num_keys = *internal_node_num_keys(parent);
    *internal_node_num_keys(parent) = original_num_keys + 1; // 父母节点中记下的键的个数+1
    if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
        printf("Need to implement splitting internal node\n");
        exit(EXIT_FAILURE);
    }

    uint32_t right_child_page_num = *internal_node_right_child(parent);
    void* right_child = get_page(table->pager, right_child_page_num);

    if (child_max_key > get_node_max_key(right_child)) {
        /* 替换右边的孩子节点*/
        *internal_node_child(parent, original_num_keys) = right_child_page_num;
        *internal_node_key(parent, original_num_keys) = get_node_max_key(right_child);
        *internal_node_right_child(parent) = child_page_num;
    } else {
        /* 为新单元格腾出空间*/
        for (uint32_t i = original_num_keys; i > index; --i) {
            void* destination = internal_node_cell(parent, i);
            void* source = internal_node_cell(parent, i-1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        *internal_node_child(parent, index) = child_page_num;
        *internal_node_key(parent, index) = child_max_key;
    }
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    /*
    创建一个新结点，并将一般的单元格迁移
    将新值插入到两个节点中的一个。
    更新父节点并创建一个新的父节点。
    */
    void* old_node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t old_max = get_node_max_key(old_node);
    uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
    void* new_node = get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);
    *node_parent(new_node) = *node_parent(old_node);
    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
    *leaf_node_next_leaf(old_node) = new_page_num;

    /*
    所有现有键加上新键应该被分割
    在旧节点(左)和新节点(右)之间平分。
    从右边开始，移动每个键到正确的位置。
    */
    for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; --i) {
        void* destination_node;
        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
            destination_node = new_node;
        } else {
            destination_node = old_node;
        }
        uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
        void* destination = leaf_node_cell(destination_node, index_within_node);

        if (i == cursor->cell_num) {
            // serialize_row(value, destination);
            serialize_row(value, leaf_node_value(destination_node, index_within_node));
            *leaf_node_key(destination_node, index_within_node) = key;
        } else if (i > cursor->cell_num) {
            memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
        } else {
            memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE);
        }
    }

    /* 更新每个节点头中的单元格计数 */
    *(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

    if (is_node_root(old_node)) {
        return create_new_root(cursor->table, new_page_num);
    } else {
        // printf("Need to implement updating parent after split\n");
        // exit(EXIT_FAILURE);
        uint32_t parent_page_num = *node_parent(old_node);
        uint32_t new_max = get_node_max_key(old_node);
        void* parent = get_page(cursor->table->pager, parent_page_num);

        update_internal_node_key(parent, old_max, new_max);
        internal_node_insert(cursor->table, parent_page_num, new_page_num);
        return ;
    }
}

// 打开数据库文件
Table* db_open(const char* filename) {
    Pager* pager = pager_open(filename);    // 初始化页管理器
    // uint32_t num_rows = pager->file_length / ROW_SIZE;  // 计算总行数

    // 初始化表结构
    Table* table = (Table*) malloc(sizeof(Table));
    table->pager = pager;
    // table->num_rows = num_rows;
    table->root_page_num = 0;

    if (pager->num_pages == 0) {
        // 新建数据库文件，初始化第0页为叶节点
        void* root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
        set_node_root(root_node, true);
    }

    return table;
}

// 通过页管理器，将每一页写入到文件中，完整数据持久化
// void pager_flush(Pager* pager, uint32_t page_num, uint32_t size) {
void pager_flush(Pager* pager, uint32_t page_num) {
    if (pager->pages[page_num] == NULL) {
        // 指向了空的页，不能写入
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }
    // 设置文件偏移量到要写入的位置，也就是该页应该在文件中的位置
    off_t offset = lseek(pager->file_descriptor,
                         page_num * PAGE_SIZE, SEEK_SET);
    
    if (offset == -1) {
        // 找不到位置
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    // 将一页写入文件中
    // ssize_t bytes_written = write(pager->file_descriptor, 
    //                                 pager->pages[page_num], size);
        ssize_t bytes_written = write(pager->file_descriptor, 
                                    pager->pages[page_num], PAGE_SIZE);

    if (bytes_written == -1) {
        // 写入失败
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

// 关闭数据库，数据写入磁盘文件，关闭文件，释放页管理器和表结构
void db_close(Table* table) {
    Pager* pager = table->pager;
    // for (uint32_t i = 0; i < num_full_pages; ++i) {
    for (uint32_t i = 0; i < pager->num_pages; ++i) {
        if (pager->pages[i] == NULL) {
            // 目前是按顺序分配页的，理论不会为NULL，这里应该是健壮性考虑
            continue;
        }
        // pager_flush(pager, i, PAGE_SIZE);// 写入一页
        pager_flush(pager, i);
        free(pager->pages[i]);  // 释放一页内存
        pager->pages[i] = NULL; // 指向每一页的指针置为空
    }

    // // 最后可能有一个页的一部分要写入到文件的末尾
    // // 在我们切换到B树之后，这就不需要了
    // uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    // if (num_additional_rows > 0) {
    //     // 存在最后不满的一页
    //     uint32_t page_num = num_full_pages;
    //     // 释放最后一页，并写入磁盘文件，
    //     if (pager->pages[page_num] != NULL) {
    //         // 写入最后几行，空的部分不写入
    //         pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
    //         free(pager->pages[page_num]);
    //         pager->pages[page_num] = NULL;
    //     }
    // }
    // 关闭文件描述符
    int result = close(pager->file_descriptor);
    if (result == -1) {
        // 关闭失败
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }
    // 确保安全，查看将全部页将没释放的释放掉
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; ++i) {
        void* page = pager->pages[i];
        if (page) {
            free(page);
            pager->pages[i] = NULL;
        }
    }
    free(pager);    // 释放页管理器结构
    free(table);    // 释放表结构
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        // 节点已满
        // printf("Need to implement splitting a leaf node.\n");
        // exit(EXIT_FAILURE);
        leaf_node_split_and_insert(cursor, key, value);
        return;
    }

    if (cursor->cell_num <= num_cells) {
        // 为新的单元格腾出空间
        for (uint32_t i = num_cells; i > cursor->cell_num; --i) {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
                    LEAF_NODE_CELL_SIZE);
        }
    }
    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));   
}

// 新建一个缓存结构，初始化并返回
InputBuffer* new_input_buffer() {
    InputBuffer* input_buffer = (InputBuffer*) malloc(sizeof(InputBuffer));
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
        db_close(table);
        exit(EXIT_SUCCESS);
    } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
        printf("Tree:\n");
        // print_leaf_node(get_page(table->pager, 0));
        print_tree(table->pager, 0, 0);
        return META_COMMAND_SUCCESS;
    } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
        printf("Constants:\n");
        print_constants();
        return META_COMMAND_SUCCESS;
    } else {
        // 返回未标识元命令
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

// 预处理插入语句，并设置处理异常的结果
PrepareResult prepare_insert(InputBuffer* input_buffer, Statement* statement) {
    statement->type = STATEMENT_INSERT; // 类型为插入语句

    // 使用strok函数用空格分隔输入
    char* keyword = strtok(input_buffer->buffer, " ");  // 应该为insert
    char* id_string = strtok(NULL, " ");    // id的字符
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    if (id_string == NULL || username == NULL || email == NULL) {
        // 输入语法有误
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_string);
    if (id < 0) {
        // id为负
        return PREPARE_NEGATIVE_ID;
    }
    // 判断字符长度是否过长
    if (strlen(username) > COLUMN_USERNAME_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }
    // 将解析结果写入到一行的结构体中
    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

// 解析命令，将解析结果存到statement中，返回解析结果
PrepareResult prepare_statement(InputBuffer* input_buffer, 
                                Statement* statement) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    }
    if (strcmp(input_buffer->buffer, "select") == 0) {
        // 以select开头
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

// 执行插入语句，返回是否执行结果的枚举
ExecuteResult execute_insert(Statement* statement, Table* table) {
    void* node = get_page(table->pager, table->root_page_num);
    uint32_t num_cells = (*leaf_node_num_cells(node));
    // if (num_cells >= LEAF_NODE_MAX_CELLS) {
    //     return EXECUTE_TABLE_FULL;
    // }

    // 一行的结构体存放在statement中，先取出
    Row* row_to_insert = &(statement->row_to_insert);
    uint32_t key_to_insert = row_to_insert->id;
    Cursor* cursor = table_find(table, key_to_insert);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_insert) {
            return EXECUTE_DUPLICATE_KEY;
        }
    }

    // 使用游标，序列化一行并插入到表中
    // serialize_row(row_to_insert, cursor_value(cursor));
    // table->num_rows += 1;   // 表中的行数+1
    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
    free(cursor);

    return EXECUTE_SUCCESS;
}

// 执行查询语句，查询一个表中的全部内容，并打印到控制台
ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table);

    Row row;
    // for (uint32_t i = 0; i < table->num_rows; ++i) {
    //     deserialize_row(row_slot(table, i), &row);
    //     print_row(&row);
    // }
    // 使用游标进行遍历，直到游标指向表结尾
    while (!(cursor->end_of_table)) {
        // 从内存中反序列化到行结构体中
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);    // 打印行
        cursor_advance(cursor); // 游标后移一位
    }

    free(cursor);

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
    // Table* table = new_table();
    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char* filename = argv[1];
    Table* table = db_open(filename);

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
            case PREPARE_NEGATIVE_ID:
                printf("ID must be positive.\n");
                continue;
            case PREPARE_STRING_TOO_LONG:
                printf("String is too long.\n");
                continue;
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Could not parse statement. \n");
                continue;
            case PREPARE_UNRECOGNIZED_STATEMENT:
                printf("Unrecognized keyword at start of '%s'.\n", 
                        input_buffer->buffer);
                continue;
        }
        // 执行sql语句
        switch (execute_statement(&statement, table)) {
            case EXECUTE_SUCCESS:
                printf("Executed.\n");
                break;
            case EXECUTE_DUPLICATE_KEY:
                printf("Error: Duplicate key.\n");
                break;
            case EXECUTE_TABLE_FULL:
                printf("Error: Table full.\n");
                break;
        }
    }
}
