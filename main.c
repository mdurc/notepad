
/*** includes ***/
#include <string.h>
#include <unistd.h> /* read, write, file descriptors */
#include <termios.h> /* terminal customizing */
#include <stdio.h> /* printf, perror */
#include <stdlib.h> /* exit, atexit */
#include <sys/ioctl.h> /* ioctl, TIOCGWINSZ */
#include <fcntl.h> /* for saving to disk */

/*** defines ***/
// for 'q', ascii value is 113, and ctrl-q is 17
// 113 in binary is 0111 0001 : 0x71
// 17  in binary is 0001 0001, which is a bit mask of 0x1F
// Macro for CTRL values:
#define CTRL_KEY(k) ((k) & 0x1F)
#define TAB_STOP 8


/*** function prototypes ***/
struct abuf;
void editor_refresh_screen();
void editor_keypress_handler();

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
    char* filename;
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

    // ---- I think this is optional for a visual effect:
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
    state.filename = NULL;
    if(get_window_size(&state.screen_rows, &state.screen_cols) == -1) error("get_window_size");
    --state.screen_rows; // room for status bar
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

void editor_append_row(char* line, size_t len){
    // allocate space for a new row
    state.row = realloc(state.row, sizeof(erow) * (state.num_rows + 1));

    int curr = state.num_rows;
    state.row[curr].size = len;
    state.row[curr].chars = malloc(len + 1); // room for null char
    memcpy(state.row[curr].chars, line, len);
    state.row[curr].chars[len] = '\0';

    state.row[curr].rsize = 0;
    state.row[curr].render = NULL;
    editor_update_row(&state.row[curr]);

    ++state.num_rows;
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

/*** editor operations ***/
// doesnt have to worry about how erow is modified
void editor_insert_char(char c){
    // If we allow the ability to go on the first tilde below the file
    //if(state.cy == state.num_rows){
    //    editor_append_row("", 0);
    //}

    editor_row_insert_char(&state.row[state.cy], state.cx, c);
    ++state.cx;
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

        editor_append_row(line, length);
    }

    free(line);
    fclose(fp);
}

void editor_save(){
    // TODO status message update to indicate save
    if(state.filename == NULL) return; // TODO

    int len;
    char *buf = editor_rows_to_string(&len);
    // 0644 are standard permissions for text files
    int fd = open(state.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                return;
            }
        }
        close(fd);
    }
    free(buf);
}

/*** input ***/

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
    char c;
    if(read(STDIN_FILENO, &c, 1) == -1) error("read");

    switch(c){
        case CTRL_KEY('c'):
            //clear_screen();
            exit(0);
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
        case 127: // backspace

            break;
        case '-':
        case '=':
        case ']':
        case '\\':
            move_cursor(c);
            // TODO: delete key
            break;
        default:
            editor_insert_char(c);
            break;
    }
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
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
            state.filename ? state.filename : "[No Name]", state.num_rows);
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
}

void editor_refresh_screen(){
    editor_scroll();

    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6); // hide cursor
    ab_append(&ab, "\x1b[H", 3); // reposition cursor to top of screen
    //clear_screen(&ab);

    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);

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


/*** program init ***/
int main(int argc, char** argv){
    enable_raw();
    init_editor();
    if(argc >= 2){
        editor_open(argv[1]);
    }
    write(STDOUT_FILENO, "\x1b[2J", 4);

    while (1){
        editor_refresh_screen();
        editor_keypress_handler();
    }

    return 0;
}
