
/*** includes ***/
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h> /* read, write, file descriptors */
#include <termios.h> /* terminal customizing */
#include <stdio.h> /* printf, perror */
#include <stdlib.h> /* exit, atexit */
#include <sys/ioctl.h> /* ioctl, TIOCGWINSZ */
#include <fcntl.h> /* for saving to disk */
#include <time.h> /* for saving to disk */

/*** defines ***/
// for 'q', ascii value is 113, and ctrl-q is 17
// 113 in binary is 0111 0001 : 0x71
// 17  in binary is 0001 0001, which is a bit mask of 0x1F
// Macro for CTRL values:
#define CTRL_KEY(k) ((k) & 0x1F)
#define TAB_STOP 8
#define QUIT_TIMES 0


/*** function prototypes ***/
struct abuf;
void editor_refresh_screen();
void editor_keypress_handler();
void editor_set_status_msg(const char* fmt, ...);
char* editor_prompt(char* prompt);

/*** data ***/

// editor row
typedef struct erow {
    int size;
    int rsize;
    char* chars;
    char* render;
} erow;

struct state {
    int cx, cy; // cursor positions (now relative to the file)
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


/*** terminal data ***/
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

/*** terminal setup **/
void error(const char* s){
    //clear_screen();
    perror(s);
    exit(1); // indicate failure with non-zero
}

void disable_raw(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.term_defaults) == -1){
        error("tcsetattr");
    }
    //printf("Disabling raw mode\n");
}

// disables flags for cannonical mode
// so we can read bytes as they are entered in stdin
// and don't have to wait for "enter"
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

    // ---- TODO: I think i need to use this for timing, and motions
    // by default, VTIME is 0 and VMIN is 1, so it blocks processes until an input is registered
    // If we want to simulate some animation, we can use the following, to create a ticking effect
    // Note that anywhere that uses read() will now continously release '\0' and this will require the use of a while != 1, read

    // Set control characters (array of bytes)
    // nbytes specied by read() is the amount of data we hope to get, VMIN is the amount we will settle for.
    //attr.c_cc[VMIN] = 0;

    // specifies the time to read "instantaneous" series of inputs before processing it as
    // a combined input data. Processed in tenths of a second. so this is set to 1/10 of a second, or 100 milliseconds.
    //attr.c_cc[VTIME] = 1; // this requires us to rewrite main to process indefinitely
    // ----


    // set these changes by flushing stdin and then setting the changes
    // if there is leftover input, it will be flushed and wont be fed into the terminal as a bunch of commands
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &attr) == -1) error("tcsetattr");
}

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

/*** row operations ***/
int editor_row_cx_to_rx(erow* row, int cx){
    int rx = 0;
    int i;
    for(i=0; i<cx; ++i){
        // find how many spaces until the end of the current tabstop, even if we are in the middle
        if(row->chars[i] == '\t')
            rx += (TAB_STOP -1) - (rx % TAB_STOP);
        ++rx;
    }
    return rx;
}

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
}

void editor_insert_row(int row_num, char* line, size_t len){
    if(row_num < 0 || row_num > state.num_rows) return;

    // allocate space for a new row
    state.row = realloc(state.row, sizeof(erow) * (state.num_rows + 1));

    int i;
    // note that state.row now has +1 allocated, so i is not out of bounds
    for(i=state.num_rows; i > row_num; --i){
        state.row[i] = state.row[i-1];
    }

    state.row[row_num].size = len;
    state.row[row_num].chars = malloc(len + 1); // room for null char
    memcpy(state.row[row_num].chars, line, len);
    state.row[row_num].chars[len] = '\0';

    state.row[row_num].rsize = 0;
    state.row[row_num].render = NULL;
    editor_update_row(&state.row[row_num]);

    ++state.num_rows;
}

// when they hit backspace on an empty row
void editor_free_row(erow* row){
    free(row->render);
    free(row->chars);
}

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
    }
    // clear the ending row
    state.row[i].size = 0;
    state.row[i].rsize = 0;
    state.row[i].chars = NULL;
    state.row[i].render = NULL;
    --state.num_rows;
}

// doesn't have to worry about where the cursor is
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

// when we backspace at the start, we should bring the current line to the end of the line above
void editor_row_append_string(erow* row, char* s, size_t len){
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_update_row(row);
}

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

/*** editor operations ***/
// doesnt have to worry about how erow is modified
void editor_insert_char(char c){
    // check if the file is empty
    if(state.cy == state.num_rows){
        editor_insert_row(state.num_rows, "", 0);
    }

    editor_row_insert_char(&state.row[state.cy], state.cx, c);
    ++state.cx;
    state.dirty = 1;
}

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

void editor_delete_char(){
    if (state.cy == state.num_rows) return; // on a new empty file
    erow* curr = &state.row[state.cy];
    if(state.cx > 0){
        // technically the cursor deletes the character BEHIND the currently highlighted one
        // If we used 'x' in vim, though, it would delete the CURRENT character at cx.
        editor_row_delete_char(curr, state.cx-1);
        --state.cx;
    }else if(state.cy > 0){
        state.cx = state.row[state.cy-1].size;
        editor_row_append_string(&state.row[state.cy-1], curr->chars, curr->size);
        editor_delete_row(state.cy);
        --state.cy;
    }
    state.dirty = 1;
}



/*** file i/o ***/
// expects the caller to clear the memory of returned char*
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

void editor_save(){
    if(state.filename == NULL){
        state.filename = editor_prompt("Save as: %s");
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
    editor_set_status_msg("Failed to save.");
}

/*** find ***/
void editor_find(){
    char* query = editor_prompt("Search: %s (ESC to cancel)");
    if(query == NULL) return;

    int i;
    for(i = 0; i< state.num_rows; ++i){
        erow* row = &state.row[i];
        char* match = strstr(row->render, query);
        if(match){
            state.cy = i;
            state.cx = editor_row_rx_to_cx(row, match - row->render);

            // make it so that we are at the very bottom of the file, so editor_scroll will scroll us upwards to the word
            state.rowoff = state.num_rows;
            break;
        }
    }
    free(query);
}


/*** input ***/

// for save as filename
char* editor_prompt(char* prompt){
    size_t bufsize = 128;
    char* buf = malloc(bufsize); // 1 byte per char
    size_t buflen = 0;
    buf[0] = '\0'; // empty str

    // infinite loop that only changes the status bar
    while (1) {
        editor_set_status_msg(prompt, buf);
        editor_refresh_screen();
        int c;
        if(read(STDIN_FILENO, &c, 1) == -1) error("read");

        if(c == 127){
            //backspace
            if(buflen != 0) buf[--buflen] = '\0';
        }else if(c == '\x1b'){
            // exit the prompt by pressing <esc>
          editor_set_status_msg("");
          free(buf);
          return NULL;
        }else if (c == '\r') {
            // <enter> has been pressed
            // only exit when a response has been typed
            if (buflen != 0) {
                editor_set_status_msg("");
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            // check that c is a valid character and non-control

            if (buflen == bufsize - 1) { // out of bounds
                // make more space
                bufsize *= 2; // common practice in vectors
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0'; // continously end with \0
        }
    }
}

void move_cursor(char c){

    erow* row = (state.cy >= state.num_rows) ? NULL : &state.row[state.cy];
    if(!row) error("moving on non-row"); // I have it setup so that row should never be NULL
    switch (c) {
        case '-': // h
            if(state.cx != 0){
                --state.cx;
            }else if(state.cy > 0){
                --state.cy;
                state.cx = state.row[state.cy].size;
            }
            break;
        case ']': // j
            // num rows is the amount of rows in the current file being viewed
            // so if there is no file, we cannot move down
            if(state.cy < (state.num_rows-1)){
                ++state.cy;
            }
            break;
        case '=': // k
            if(state.cy != 0){
                --state.cy;
            }
            break;
        case '\\': // l
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


// read 1 byte from STDIN, store in address of char c
void editor_keypress_handler(){
    static int quit_times = QUIT_TIMES;

    char c;
    if(read(STDIN_FILENO, &c, 1) == -1) error("read");

    switch(c){
        case CTRL_KEY('c'):
            //clear_screen();
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
        case '/':
            editor_find();
            break;
        case 127: // backspace
            editor_delete_char();
            break;
        case '\r': // enter
            editor_insert_newline();
            break;
        case '-':
        case '=':
        case ']':
        case '\\':
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

/*** append buffer ***/
struct abuf {
    char* b;
    int len;
};
#define ABUF_INIT {NULL, 0} // empty memory buffer

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
void ab_free(struct abuf* ab){
    free(ab->b);
}

/*** output ***/

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

void editor_draw_rows(struct abuf* ab){
    int i;
    for(i=0;i<state.screen_rows; ++i){
        int filerow = i + state.rowoff;
        if(i >= state.num_rows){
            // check if we are outside the range of the currently edited number of rows
            ab_append(ab, "~", 1);
        }else{
            int len = state.row[filerow].rsize - state.coloff;
            if(len < 0) len = 0;
            if(len > state.screen_cols) len = state.screen_cols;

            // include vertical and horizontal offsets (horizontal is by starting the line string at offset
            ab_append(ab, state.row[filerow].render + state.coloff, len);
        }
        ab_append(ab, "\x1b[K", 3); // erase to the right of current line
        ab_append(ab, "\r\n", 2); // dont do newline at bottom
    }
}

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

void editor_refresh_screen(){
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6); // hide cursor
    ab_append(&ab, "\x1b[H", 3); // reposition cursor to top of screen
    //clear_screen(&ab);

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


/*** program init ***/
int main(int argc, char** argv){
    enable_raw();
    init_editor();
    if(argc >= 2){
        editor_open(argv[1]);
    }

    editor_set_status_msg("movement: -, =, ], \\");

    while (1){
        editor_refresh_screen();
        editor_keypress_handler();
    }

    return 0;
}
