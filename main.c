
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
// secondary keywords are marked by ending with a '|'
// ends in NULL so that we can loop through items without knowing the size of array
char* C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
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
} state;

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
int editor_read_key();
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

/* ----------------------------------------- utility -------------------------------------------*/
// error message utility function for exiting with error code and printing error
void error(const char* s){
    perror(s);
    exit(1); // indicate failure with non-zero
}

/* ------------------------------------ terminal functions ------------------------------------ */
// Only used for calculating the size of the terminal window when IOCtl is not available
int get_cursor_position(int* rows, int* cols){
    char buf[32]; // for reading the response
    unsigned int i = 0;

    // ESC [ Ps n is for a DSR (Device Status Report)
    // Argument of 6 will query the terminal for cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    // The reply is an escape sequence (can be found using read(stdin))
    // 27 91 ('[') 52 ('4') 49 ('1') 59 (';') 54 ('6') 54 ('6') 82 ('R')
    // which is [41;66R, for row and then column

    while(i < sizeof(buf) - 1){
        if (read(STDIN_FILENO, buf + i, 1)!= 1) break;
        if (buf[i] == 'R') break;
        ++i;
    }
    buf[i] = '\0'; // null-terminate

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    // note that if you print out the response, start from [1] so that you aren't printing it as an escape sequence again
    //printf("\r\nbuf+1 : '%s'\r\n", buf+1); // [41;66
    if (sscanf(buf+1, "[%d;%d", rows, cols) != 2) return -1;

    return 0;
}

// use IOCtl to find window size, otherwise move to bottom right and get the cursor position
int get_window_size(int* rows, int* cols){
    struct winsize ws; // from sys/ioctl.h
    // TIOCGWINSZ: Terminal IOCtl Get WINdow SiZe
    // IOCtl: Input/Output Control
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        // if ioctl fails in this terminal, we have to use a different method:
        // move cursor to the bottom right corner with C and B commands, both designed to
        // stop the cursor from going past the edge of the screen.
        // Note that we dont do <esc>[999;999H because we might go off the screen.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;

        // now retrieve cursor position
        return get_cursor_position(rows, cols);
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

// should be used after enable_raw(), so that the defaults of the terminal are restored
void disable_raw(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.term_defaults) == -1){
        error("tcsetattr");
    }
}

// disables flags for cannonical mode so we can read bytes as they are entered in stdin and don't have to wait for "enter"
void enable_raw(){
    if (tcgetattr(STDIN_FILENO, &state.term_defaults) == -1) error("tcgetattr");
    atexit(disable_raw); // accepts a function ptr void (*) (void)

    // make a struct to retrieve the attributes of terminal
    struct termios attr = state.term_defaults;
    // the attributes of termios struct include a c_lflag for local modes of type
    // "tcflag_t", which specifies a ton of flags, one flag for each bit
    // We want to use bit-masking to set specific attribute flags
    // #define ECHO 0x00000008, so in order to disable it, mask with &, and ~ECHO
    // ICANON will allow us to read byte-by-byte
    // ISIG will allow us to disable signals like ctrl-c and ctrl-z
    // IXON will allow us to disable software flow control signals
    // IEXTEN for ctrl-v and ctrl-o
    // ICRNL to make carriage returns the default
    // OPOST to disable auto-output processing features like converting \n to \r\n, to move character to the start of the line, and down. This will require us to manually print \r\n instead of \n.
    attr.c_iflag &= ~(IXON | ICRNL); // input modes
    attr.c_oflag &= ~(OPOST); // input modes
    attr.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); // local modes

    // miscellaneous flags (not really noticeable)
    attr.c_iflag &= ~(BRKINT | INPCK | ISTRIP);
    attr.c_cflag |= (CS8); // sets the character size to 8 bits per byte (default)

    // ---- Useful for parsing escape sequences ----- :

    // by default, VTIME is 0 and VMIN is 1, so it blocks processes until an input is registered
    // If we want to simulate some animation, we can use the following, to create a ticking effect
    // Note that anywhere that uses read() will now continously release '\0' and this will require the use of a while != 1, read

    // Set control characters (array of bytes)
    // nbytes specied by read() is the amount of data we hope to get, VMIN is the amount we will settle for.
    attr.c_cc[VMIN] = 0;

    // specifies the time to read "instantaneous" series of inputs before processing it as
    // a combined input data. Processed in tenths of a second. so this is set to 1/10 of a second, or 100 milliseconds.
    attr.c_cc[VTIME] = 1; // this requires us to rewrite main to process indefinitely
    // ----


    // set these changes by flushing stdin and then setting the changes
    // if there is leftover input, it will be flushed and wont be fed into the terminal as a bunch of commands
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attr) == -1) error("tcsetattr");
}


/* ------------------------------------ row operations ------------------------------------ */
// Example: moving cursor forward TAB_STOP spaces when placed in the middle of a tab.
int editor_row_cx_to_rx(erow* row, int cx){
    int rx = 0;
    int i;
    for(i=0; i<cx; ++i){
        // find how many spaces until the end of the current tabstop, even if we are in the middle
        if(row->chars[i] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        ++rx;
    }
    return rx;
}

// Used for locating the cx when we have the render x-position during searching.
int editor_row_rx_to_cx(erow* row, int rx){
    int curr_rx = 0;
    int cx;
    for(cx = 0; cx < row->size; ++cx){
        if(row->chars[cx] == '\t'){
            curr_rx += (TAB_STOP - 1) - (curr_rx % TAB_STOP);
        }
        ++curr_rx;
        if(curr_rx > rx) return cx;
    }
    return cx; // just in case the parameters were out of bounds
}

// move row->chars into the "render" characters, adjusting for tabs
void editor_update_row(erow* row){
    int tabs = 0;
    int i, j;
    for (j = 0; j < row->size; ++j){
        if (row->chars[j] == '\t') ++tabs;
    }
    free(row->render);
    // tabs will take up a maximum of 8 characters
    // row->size already counts 1, so do tabs*7
    row->render = malloc(row->size + tabs*(TAB_STOP-1) + 1);

    for(i=0, j=0;j<row->size;++j){
        if (row->chars[j] == '\t') {
            row->render[i++] = ' ';
            while (i % TAB_STOP != 0) row->render[i++] = ' ';
        } else {
            row->render[i++] = row->chars[j];
        }
    }
    row->render[i] = '\0';
    row->rsize = i;

    // after updating the render characters, update the syntax that is based on render
    editor_update_syntax(row);
}

// insert a row in position: row_num, with contents: line, and length: len
void editor_insert_row(int row_num, char* line, size_t len){
    if(row_num < 0 || row_num > state.num_rows) return;

    // allocate space for a new row
    state.row = realloc(state.row, sizeof(erow) * (state.num_rows + 1));

    int i;
    // note that state.row now has +1 allocated, so i is not out of bounds
    for(i=state.num_rows; i > row_num; --i){
        state.row[i] = state.row[i-1];
        ++state.row[i].idx; // adjust index
    }

    state.row[row_num].idx = row_num;

    state.row[row_num].size = len;
    state.row[row_num].chars = malloc(len + 1); // room for null char
    memcpy(state.row[row_num].chars, line, len);
    state.row[row_num].chars[len] = '\0';

    state.row[row_num].rsize = 0;
    state.row[row_num].render = NULL;
    state.row[row_num].hl = NULL;
    state.row[row_num].hl_open_comment = 0;
    editor_update_row(&state.row[row_num]); // update the render characters in state

    ++state.num_rows;
}

// freeing memory of a given row, when the row is deleted
void editor_free_row(erow* row){
    free(row->render);
    free(row->chars);
    free(row->hl);
}

// when backspace on an empty row
void editor_delete_row(int row_num){
    if (row_num < 0 || row_num >= state.num_rows) return;
    // free the memory
    editor_free_row(&state.row[row_num]);

    // remove the row from the array
    int i;
    for(i=row_num; i<state.num_rows-1; ++i){
        // all attributes should be copied,
        // ptrs will be copied and it should be fine
        state.row[i] = state.row[i+1];
        --state.row[i].idx; // update index of the remaining rows
    }
    // clear the ending row
    state.row[i].size = 0;
    state.row[i].rsize = 0;
    state.row[i].chars = NULL;
    state.row[i].render = NULL;
    --state.num_rows;
}

// simply inserts character into char array. Doesn't have to worry about where the cursor is
void editor_row_insert_char(erow* row, int column, char c){
    if(column < 0 || column > row->size) column = row->size;
    // add room for new character and null-byte
    row->chars = realloc(row->chars, row->size + 2);

    ++row->size;
    int i;
    // ripple through and carry all chars to the end
    for(i=column; i<row->size; ++i){
        char temp = row->chars[i];
        row->chars[i] = c;
        c = temp;
    }
    row->chars[i] = '\0';
    editor_update_row(row);
}

// backspace on a non-empty line: append the contents of current line to the end of previous line
void editor_row_append_string(erow* row, char* s, size_t len){
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
}

// simply removes character from char array in row
void editor_row_delete_char(erow* row, int column){
    if(column < 0 || column  >= row->size) return;

    int i;
    // ripple through and carry all chars after column back one space
    for(i=column; i<row->size-1; ++i){
        row->chars[i] = row->chars[i+1];
    }
    // get rid of final leftover char with null byte
    row->chars[i] = '\0';
    --row->size;
    editor_update_row(row);
}


/* ------------------------------------ editor operations ------------------------------------ */

// adjusts cursor positions of inserting a character
void editor_insert_char(char c){
    // check if the file is empty
    if(state.cy == state.num_rows){
        editor_insert_row(state.num_rows, "", 0);
    }

    editor_row_insert_char(&state.row[state.cy], state.cx, c);
    ++state.cx;
    state.dirty = 1;
}

// handles newline characters by creating a new row, possibly splitting the current row.
// adjusts the cursor positions as well.
void editor_insert_newline(){
    if(state.cx == 0){
        // we are newlining at the start of a line
        editor_insert_row(state.cy, "", 0);
    }else{
        erow *row = &state.row[state.cy];
        // The string on the new line will be pointed to at: row->chars + state.cx
        // With a length of row->size - state.cx
        editor_insert_row(state.cy + 1, &row->chars[state.cx], row->size - state.cx);
        row = &state.row[state.cy];     // This memory location has been "realloced" so we have to redefine the ptr
                                        // because it may have moved the memory entirely
        row->size = state.cx; // the new top portion will have size of cx, where it breaks
        row->chars[row->size] = '\0';
        editor_update_row(row);
    }
    ++state.cy; // move to lower row, note that the size and content has been handled from editor_insert_row(..)
    state.cx = 0; // goto start of the new line
}

// Adjusts cursor positions, delegates to row deletion functions
void editor_delete_char(){
    if (state.cy == state.num_rows) return; // on a new empty file
    erow* curr = &state.row[state.cy];
    if(state.cx > 0){
        // technically the cursor deletes the character BEHIND the currently highlighted one
        // If we used 'x' in vim, though, it would delete the CURRENT character at cx.
        editor_row_delete_char(curr, state.cx-1);
        --state.cx;
    }else if(state.cy > 0){
        // then we are at the start of the line, and not at the beginning of file
        state.cx = state.row[state.cy-1].size;
        editor_row_append_string(&state.row[state.cy-1], curr->chars, curr->size);
        editor_delete_row(state.cy);
        --state.cy;
    }
    state.dirty = 1;
}

// Read keys, made for handling special defined keys, and multi-byte escape sequences that can be read due to changing vmin and vtime
int editor_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) error("read");
    }
    // check for escape characters
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

// initialize editor state
void init_editor(){
    state.cx = 0;
    state.cy = 0;
    state.rx = 0;
    state.rowoff = 0;
    state.coloff = 0;
    state.num_rows = 0;
    state.row = NULL;
    state.dirty = 0;
    state.filename = NULL;
    state.statusmsg[0] = '\0';
    state.statusmsg_time = 0;
    if(get_window_size(&state.screen_rows, &state.screen_cols) == -1) error("get_window_size");
    state.screen_rows -= 2; // room for status bar and msg
}

/* ------------------------------------ file input/output ------------------------------------ */
// expects the caller to clear the memory of returned char*.
// Used for saving contents of file to disk.
char* editor_rows_to_string(int* buflen){
    int total_len = 0;
    int i;
    // +1 for newline characters to be added and the final null byte
    for(i=0; i<state.num_rows; ++i){
        total_len += state.row[i].size + 1;
    }
    *buflen = total_len;

    char* buf = malloc(total_len);

    // use another pointer as a walker through the buf memory
    char* p = buf;
    for(i = 0; i<state.num_rows; ++i){
        memcpy(p, state.row[i].chars, state.row[i].size);
        p += state.row[i].size;
        *p = '\n';
        p++;
    }
    return buf;
}

// Opens file, parses file lines, fills editor state with line contents (erows).
void editor_open(char* filename){
    free(state.filename);
    int len = strlen(filename);
    state.filename = (char*) malloc(len + 1);
    strcpy(state.filename, filename); // includes null-byte

    FILE* fp = fopen(filename, "r");
    if (!fp) error("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t length;
    // using getline() because we don't know how much memory to allocate per line
    while((length = getline(&line, &linecap, fp)) != -1){
        if (length == -1) error("getline");

        // get rid of any trailing whitespace by reducing length
        while(length > 0 &&
             (line[length-1] == '\n' ||
              line[length-1]=='\r')){
            --length;
        }

        editor_insert_row(state.num_rows, line, length);
    }

    free(line);
    fclose(fp);
}

// Save file, if file doesn't exist, prompts for new file creation
void editor_save(){
    if(state.filename == NULL){
        state.filename = editor_prompt("Save as: %s", NULL);
        if(state.filename == NULL){
            editor_set_status_msg("Save cancelled");
            return;
        }
    }

    int len;
    char *buf = editor_rows_to_string(&len);
    // 0644 are standard permissions for text files
    int fd = open(state.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                editor_set_status_msg("%d bytes written to disk", len);
                state.dirty = 0;
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editor_set_status_msg("Failed to save. Error: %s", strerror(errno));
}


/* --------------------------------------- user input --------------------------------------- */

// For commands, searching, saving: opens a prompt at the bottom line of editor
char* editor_prompt(char* prompt, void (*callback)(char*, int)){
    size_t bufsize = 128;
    char* buf = malloc(bufsize); // 1 byte per char
    size_t buflen = 0;
    buf[0] = '\0'; // empty str

    // infinite loop that only changes the status bar
    while (1) {
        editor_set_status_msg(prompt, buf);
        editor_refresh_screen();

        int c = editor_read_key();

        if(c == BACKSPACE){
            //backspace
            if(buflen != 0) buf[--buflen] = '\0';
        }else if(c == '\x1b' || c == CTRL_KEY('c')){
            // exit the prompt by pressing <esc>
            editor_set_status_msg("");
            if(callback) callback(buf, c);
            free(buf);
            return NULL;
        }else if (c == '\r') {
            // <enter> has been pressed
            // only exit when a response has been typed
            if (buflen != 0) {
                editor_set_status_msg("");
                if(callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && (int)c < 128) {
            // check that c is a valid character and non-control

            if (buflen == bufsize - 1) { // out of bounds
                // make more space
                bufsize *= 2; // common practice in vectors
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0'; // continously end with \0
        }
        if(callback) callback(buf, c);
    }
}

// Moving cursor on non-insertion/deletion of characters. Purely movement characters.
void move_cursor(int c){
    erow* row = (state.cy >= state.num_rows) ? NULL : &state.row[state.cy];
    if(!row) error("moving on non-row"); // I have it setup so that row should never be NULL
    switch (c) {
        case ARROW_LEFT: // h
            if(state.cx != 0){
                --state.cx;
            }else if(state.cy > 0){
                --state.cy;
                state.cx = state.row[state.cy].size;
            }
            break;
        case ARROW_DOWN: // j
            // num rows is the amount of rows in the current file being viewed
            // so if there is no file, we cannot move down
            if(state.cy < (state.num_rows-1)){
                ++state.cy;
            }
            break;
        case ARROW_UP: // k
            if(state.cy != 0){
                --state.cy;
            }
            break;
        case ARROW_RIGHT: // l
            if(state.cx < row->size){
                ++state.cx;
            }else if(state.cy < (state.num_rows-1)){
                ++state.cy;
                state.cx = 0;
            }
            break;
    }
    // snap the horizontal to the end of each line
    row = (state.cy >= state.num_rows) ? NULL : &state.row[state.cy];
    if(!row) error("moving on non-row");

    int rowlen = row ? row->size : 0;
    if (state.cx > rowlen) {
        state.cx = rowlen;
    }
}

// read 1 byte from STDIN, store in address of int c (char). Handles all keybind specifications.
void editor_keypress_handler(){
    static int quit_times = QUIT_TIMES;

    int c = editor_read_key();

    switch(c){
        case CTRL_KEY('c'):
            if(state.dirty && quit_times > 0){
                editor_set_status_msg("WARNING!!! File has unsaved changes, press CTRL-c %d more times to quit without saving.", quit_times);
                --quit_times;
                return;
            }
            exit(0);
            break;
        case CTRL_KEY('d'):
            if(state.cy < state.num_rows - 10){
                state.cy += 10;
            }else{
                state.cy = state.num_rows - 1;
            }
            break;
        case CTRL_KEY('u'):
            if(state.cy >= 10){
                state.cy -= 10;
            }else{
                state.cy = 0;
            }
            break;
        case CTRL_KEY('s'):
            editor_save();
            break;
        case '0':
            state.cx = 0;
            break;
        case '$':
            if(state.cy < state.num_rows){
                state.cx = state.row[state.cy].size;
            }
            break;
        case 'G':
            state.cy = state.num_rows - 1;
            break;
        case CTRL_KEY('f'):
            editor_find();
            break;
        case BACKSPACE: // backspace
            editor_delete_char();
            break;
        case '\r': // enter
            editor_insert_newline();
            break;
        case '\t': // tab
            editor_insert_char(c);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            move_cursor(c);
            break;
        default:
            if(!iscntrl(c)){
                editor_insert_char(c);
            }
            break;
    }

    // reset quit times after processing other inputs
    quit_times = QUIT_TIMES;
}


/* ------------------------------------ incremental searching ------------------------------------ */
// For looping and moving to words as they are typed (callback for editor_find())
void editor_find_callback(char* query, int key){
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char* saved_hl = NULL;

    if(saved_hl){
        memcpy(state.row[saved_hl_line].hl, saved_hl, state.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if(key == '\r' || key == '\x1b' || key == CTRL_KEY('c')){
        // reset the static default values
        last_match = -1;
        direction = 1;
        return;
    }else if(key == ARROW_DOWN){
        // down
        direction = 1;
    }else if(key == ARROW_UP){
        // up
        direction = -1;
    }else{
        last_match = -1;
        direction = 1;
    }

    // start from top if there are no previous matches
    if(last_match == -1) direction = 1;

    // index of the current row we are searching
    int current = last_match;

    int i;
    for(i = 0; i< state.num_rows; ++i){
        current += direction;

        // make current wrap around the file
        if (current < 0) {
            current = state.num_rows - 1;
        } else if (current >= state.num_rows) {
            current = 0;
        }

        erow* row = &state.row[current];
        char* match = strstr(row->render, query);
        if(match){
            last_match = current;
            state.cy = current;
            state.cx = editor_row_rx_to_cx(row, match - row->render);

            // make it so that we are at the very bottom of the file, so editor_scroll will scroll us upwards to the word
            //state.rowoff = state.num_rows;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&state.row[current].hl[match-row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

// driver function for incremental search. Restores cx and cy if search is cancelled.
void editor_find(){
    int saved_cx = state.cx;
    int saved_cy = state.cy;
    int saved_coloff = state.coloff;
    int saved_rowoff = state.rowoff;
    char* query = editor_prompt("Search: %s (ESC to cancel)", editor_find_callback);

    if(query){
        free(query);
    } else {
        state.cx = saved_cx;
        state.cy = saved_cy;
        state.coloff = saved_coloff;
        state.rowoff = saved_rowoff;
    }
}

/* ------------------------------------- syntax highlighting ----------------------------------- */
// moves through erow's render array and hl array, assigning highlight descriptions for each char within render, in hl
// in charge of filling hl array
void editor_update_syntax(erow* row){
    // hl points to type uint8_t which is an unsigned char, which is 1 byte
    row->hl = realloc(row->hl, row->rsize /* * sizeof(unsigned char) */);
    memset(row->hl, HL_NORMAL, row->rsize);

    int prev_sep = 1; // start of row should act as a valid separator
    int in_string = 0; // flag for inside double or single quotes.
                       // it will equal the double or single quote so we can highlight: "jack's"
    int in_comment = (row->idx > 0 && state.row[row->idx - 1].hl_open_comment);
    int i;
    for(i=0;i<row->rsize;++i){
        char c = row->render[i];
        uint8_t prev_hl = (i > 0) ? row->hl[i-1]:HL_NORMAL;

        if(!in_string && !in_comment && !strncmp(&row->render[i], "//", 2)){
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break; // don't try to color anything else, if it is in a comment line
        }

        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (!strncmp(&row->render[i], "*/", 2)) {
                memset(&row->hl[i], HL_MLCOMMENT, 2);
                ++i;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                continue;
            }
        } else if (!strncmp(&row->render[i], "/*", 2)) {
            memset(&row->hl[i], HL_MLCOMMENT, 2);
            ++i;
            in_comment = 1;
            continue;
        }

        if(in_string){
            row->hl[i] = HL_STRING;
            if(c == '\\' && i+1 < row->rsize){
                // If there is a backslash, move to the next character
                // then color the next character as well.

                // Accounts for "hi\"there"
                row->hl[i+1] = HL_STRING;
                ++i;
                continue;
            }
            if(c == in_string) in_string = 0;
            prev_sep = 1; // ended string highlighting
            continue;
        }else{
            if(c == '"' || c == '\''){
                in_string = c;
                row->hl[i] = HL_STRING;
                continue;
            }
        }

        if(isdigit(c) && ((prev_sep || prev_hl == HL_NUMBER) ||
                          (c == '.' && prev_hl == HL_NUMBER))){
            row->hl[i] = HL_NUMBER;
            prev_sep = 0; // we are currently highlighting the number
            continue;
        }

        if(prev_sep){
            int j;
            for(j=0; C_HL_keywords[j]; ++j){
                int keyword_len = strlen(C_HL_keywords[j]);
                int is_kw2 = C_HL_keywords[j][keyword_len - 1] == '|';
                if (is_kw2) --keyword_len;

                if (!strncmp(&row->render[i], C_HL_keywords[j], keyword_len) &&
                        is_separator(row->render[i + keyword_len])) {
                    memset(&row->hl[i], is_kw2 ? HL_KEYWORD2 : HL_KEYWORD1, keyword_len);
                    i += (keyword_len-1); // outer loop increments
                    break;
                }
            }
            if (C_HL_keywords[j] != NULL) {
                // means that we had to break out of previous loop
                prev_sep = 0; // just ended on a keyword
                continue;
            }
        }

        prev_sep = is_separator(c);
    }

    // check if we closed the multi-line comment or not
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if(changed && row->idx + 1 < state.num_rows){
        editor_update_syntax(&state.row[row->idx + 1]);
    }
}

// returns the ansi code for integers from editor_highlight
int editor_syntax_to_color(uint8_t hl){
    switch(hl){
        case HL_MLCOMMENT:
        case HL_COMMENT: return 36; // cyan
        case HL_KEYWORD1: return 33; // yellow
        case HL_KEYWORD2: return 32; // green
        case HL_STRING: return 35; // magenta
        case HL_NUMBER: return 31; // forground red
        case HL_MATCH: return 34; // bright blue
        default: return 37; // foreground white
    }
}

// for identifying when a number is isolated vs within a string
int is_separator(int c){
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}


/* --------------------------------------- append buffer --------------------------------------- */
// For appending a string to the end of the current append buffer
void ab_append(struct abuf* ab, const char* s, int len){
    // allocate for the new string
    char* new = realloc(ab->b, ab->len + len);
    if(new == NULL) return;

    // copy new string s into new[.], strcpy would also work
    // memcpy because len is actually size in bytes, and we don't want to just be copying characters
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

// deallocate the malloced char array in append buffer
void ab_free(struct abuf* ab){
    free(ab->b);
}

/* --------------------------------------- drawing to screen --------------------------------------- */

// Scrolls when file is larger than screen, when cursor is off, or trying to move off the screen
void editor_scroll() {
    state.rx = 0;
    if (state.cy < state.num_rows) {
        state.rx = editor_row_cx_to_rx(state.row + state.cy, state.cx);
    }

    // Vertical -----
    // Since cy is relative to the file, we need to subtract
    // the current file row offset to see if it is off-screen
    if ((state.cy - state.rowoff) < 0) {
        state.rowoff = state.cy;
    }

    // once the cursor position is off the screen of rows,
    // the rowoff will increment (but since we might have jumped
    // several lines down, we should calculate the new offset).
    if ((state.cy - state.rowoff) >= state.screen_rows) {
        state.rowoff = state.cy - state.screen_rows + 1;
    }

    // Horizontal -----
    if((state.rx - state.coloff) < 0){
        state.coloff = 0;
    }
    if((state.rx - state.coloff) >= state.screen_cols){
        state.coloff = state.rx - state.screen_cols + 1;
    }
}

// write all the contents of the append buffer to the terminal
void editor_draw_rows(struct abuf* ab){
    int i;
    for(i=0;i<state.screen_rows; ++i){
        int filerow = i + state.rowoff;
        if(filerow >= state.num_rows){
            // check if we are outside the range of the currently edited number of rows
            ab_append(ab, "~", 1);
        }else{
            int len = state.row[filerow].rsize - state.coloff;
            if(len < 0) len = 0;
            if(len > state.screen_cols) len = state.screen_cols;

            // move through the portion of the row that should be displayed on screen
            char* p = state.row[filerow].render + state.coloff; // render array ptr
            uint8_t* highlights = state.row[filerow].hl + state.coloff; // hl array ptr

            // only change color when it is different from the previous character
            int curr_color = -1;

            int i;
            for(i=0;i<len;++i){
                if(iscntrl(p[i])){ // for non-printable characters, make them print nicely
                    char symbol = (p[i] <= 26) ? '@' + p[i] : '?';
                    ab_append(ab, "\x1b[7m", 4); // invert colors
                    ab_append(ab, &symbol, 1);
                    ab_append(ab, "\x1b[m", 3); // revert colors
                    if (curr_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", curr_color);
                        ab_append(ab, buf, clen); // add current color back
                    }
                }else if(highlights[i] == HL_NORMAL){   // making the common case fast
                    if(curr_color != -1){
                        curr_color = -1;
                        ab_append(ab, "\x1b[39m", 5); // default color
                    }
                    ab_append(ab, p + i, 1);
                }else{
                    int h_color = editor_syntax_to_color(highlights[i]);
                    if(curr_color != h_color){
                        curr_color = h_color;

                        char sequence_buf[16];
                        int sequence_length = snprintf(sequence_buf, sizeof(sequence_buf), "\x1b[%dm", h_color);
                        ab_append(ab, sequence_buf, sequence_length); // update color
                    }
                    ab_append(ab, p + i, 1);
                }
            }
            ab_append(ab, "\x1b[39m", 5); // reset to default color
        }
        ab_append(ab, "\x1b[K", 3); // erase to the right of current line
        ab_append(ab, "\r\n", 2); // dont do newline at bottom
    }
}

// write a status bar to the last line of the terminal
void editor_draw_status_bar(struct abuf* ab){
    ab_append(ab, "\x1b[7m", 4); // invert colors escape sequence
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
            state.filename ? state.filename : "[No Name]", state.num_rows, state.dirty ? "[+]" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
            state.cy + 1, state.num_rows);
    if (len > state.screen_cols) len = state.screen_cols;
    ab_append(ab, status, len);
    for(;len<state.screen_cols; ++len){
        if(state.screen_cols - len == rlen){
            ab_append(ab, rstatus, rlen);
            break;
        }
        ab_append(ab, " ", 1); // color the bar
    }
    ab_append(ab, "\x1b[m", 3); // undo color invert
    ab_append(ab, "\r\n", 2);
}

// write a content to the space for where messages and prompts come up
void editor_draw_msg_bar(struct abuf* ab){
    ab_append(ab, "\x1b[K", 3);
    int msg_len = strlen(state.statusmsg);
    if (msg_len > state.screen_cols) msg_len = state.screen_cols;
    
    // Only write it if it is 5 seconds old
    // Note that it will stay on the screen until editor_refresh_screen() is called
    // which is on user input, which is good.
    if (msg_len && (time(NULL) - state.statusmsg_time < 5))
        ab_append(ab, state.statusmsg, msg_len);
}

// uses variable argument number, from stdarg.h
// This is for passing arguments like: msg("%d bytes", len);
void editor_set_status_msg(const char* fmt, ...){
    // variable list of arguments
    va_list ap; // arg pointer
    va_start(ap, fmt); // bring pointer to the start of the list of args

   // stores in statusmsg what the formatted output would normally be if the
   // arguments were used in printf
    vsnprintf(state.statusmsg, sizeof(state.statusmsg), fmt, ap);
    va_end(ap); // free
    state.statusmsg_time = time(NULL);
}

// update screen by drawing all contents (occurs on any keypress)
void editor_refresh_screen(){
    editor_scroll();

    struct abuf ab;
    ab.b = NULL;
    ab.len = 0;

    ab_append(&ab, "\x1b[?25l", 6); // hide cursor
    ab_append(&ab, "\x1b[H", 3); // reposition cursor to top of screen

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_msg_bar(&ab);

    // the terminal uses 1-indexing, so (1,1) is the top left corner
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (state.cy - state.rowoff) + 1, (state.rx - state.coloff) + 1);
    // append this escape sequence, so that we move cursor to the designated location
    // Note that this is moving the cursor to a position relative to the screen, not the file
    ab_append(&ab, buf, strlen(buf));


    ab_append(&ab, "\x1b[?25h", 6); // show cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    ab_free(&ab);
}


/* --------------------------------------- program init --------------------------------------- */
int main(int argc, char** argv){
    enable_raw();
    init_editor();
    if(argc >= 2){
        editor_open(argv[1]);
    }

    editor_set_status_msg("movement: arrow keys");

    while (1){
        editor_refresh_screen();
        editor_keypress_handler();
    }

    return 0;
}
