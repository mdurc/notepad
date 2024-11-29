#ifndef EDITOR_H
#define EDITOR_H

/* ------------------------------------ includes ------------------------------------ */
#include <ctype.h> /* iscntrl for inputs */
#include <stdarg.h> /* va_list, va_start, ... */
#include <string.h> /* memcpy, strlen, strcpy, strerror, strstr */
#include <unistd.h> /* read, write, file descriptors */
#include <termios.h> /* terminal customizing */
#include <stdio.h> /* printf, perror */
#include <stdlib.h> /* exit, atexit */
#include <sys/ioctl.h> /* ioctl, TIOCGWINSZ */
#include <fcntl.h> /* for saving to disk */
#include <time.h> /* for saving to disk */
#include <errno.h> /* errno, EAGAIN */

/* ------------------------------------ defines ------------------------------------ */
// for 'q', ascii value is 113, and ctrl-q is 17
// 113 in binary is 0111 0001 : 0x71
// 17  in binary is 0001 0001, which is a bit mask of 0x1F
#define CTRL_KEY(k) ((k) & 0x1F) // macro for reading ctrl keypresses
#define TAB_STOP 4
#define QUIT_TIMES 3


/* ------------------------------------ data ------------------------------------ */

// Modal editing
enum editor_modes{
    NORMAL_MODE = 0,
    INSERT_MODE,
    VISUAL_MODE,
    COMMAND_MODE
};

// defining special keys that are used
enum editor_key{
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

// Will be held in hl within erow
enum editor_highlight{
    HL_NORMAL = 0,
    HL_MLCOMMENT,
    HL_COMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

// append buffer: what is written on every refresh
struct abuf {
    char* b;
    int len;
};

// editor row
typedef struct erow {
    int idx; // for multi-line comments, knowing what index the row is
    int hl_open_comment; // flag for being in ml_comment
    int size;
    int rsize;
    char* chars;
    char* render; // used for how tabs and other special characters are rendered

    // uint8_t is unsiged char, 1 byte
    uint8_t* hl; // what color to apply to each character in render (from editor_highlight enum)
} erow;

struct state {
    int mode;   // for modal editing
    int cx, cy; // cursor positions (now relative to the file currently being read)
    int rx;     // index for render field, used for adjusting for tabs
    int rowoff; // row offset for vertical scrolling
    int coloff; // col offset for horizontal scrolling
    int screen_rows;
    int screen_cols;
    int num_rows;
    erow* row;
    int dirty;  // flag for if current file has been modified
    char* filename;
    char statusmsg[80]; // 79 characters, 1 null byte
    time_t statusmsg_time;
    struct termios term_defaults;
};

extern struct state state;

/* ------------------------------ function prototypes ------------------------------ */
void error(const char* s);
int get_cursor_position(int* rows, int* cols);
int get_window_size(int* rows, int* cols);
void disable_raw();
void enable_raw();
int editor_row_cx_to_rx(erow* row, int cx);
int editor_row_rx_to_cx(erow* row, int rx);
void editor_update_row(erow* row);
void editor_insert_row(int row_num, char* line, size_t len);
void editor_free_row(erow* row);
void editor_delete_row(int row_num);
void editor_row_insert_char(erow* row, int column, char c);
void editor_row_append_string(erow* row, char* s, size_t len);
void editor_row_delete_char(erow* row, int column);
void editor_insert_char(char c);
void editor_insert_newline();
void editor_delete_char();
void editor_delete_word();
//int editor_read_key();
void init_editor();
char* editor_rows_to_string(int* buflen);
void editor_open(char* filename);
void editor_save();
char* editor_prompt(char* prompt, void (*callback)(char*, int));
void move_cursor(int c);
void editor_keypress_handler();
void editor_find_callback(char* query, int key);
void editor_find();
void ab_append(struct abuf* ab, const char* s, int len);
void ab_free(struct abuf* ab);
void editor_scroll();
void editor_draw_rows(struct abuf* ab);
void editor_draw_status_bar(struct abuf* ab);
void editor_draw_msg_bar(struct abuf* ab);
void editor_set_status_msg(const char* fmt, ...);
void editor_refresh_screen();

void editor_update_syntax(erow* row);
int editor_syntax_to_color(uint8_t hl);
int is_separator(int c);

// VIM:
void read_normal_mode(int c);
void read_insert_mode(int c);
void read_visual_mode(int c);
void read_command_mode();

void move_previous_word();
void move_next_word();
void move_end_next_word();

void backwards_F(int c);
void forwards_F(int c);
void backwards_T(int c);
void forwards_T(int c);


#endif
