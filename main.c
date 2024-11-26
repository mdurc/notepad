
/*** includes ***/
#include <string.h>
#include <unistd.h> /* read, write, file descriptors */
#include <termios.h> /* terminal customizing */
#include <stdio.h> /* printf, perror */
#include <stdlib.h> /* exit, atexit */
#include <sys/ioctl.h> /* ioctl, TIOCGWINSZ */

/*** defines ***/
// for 'q', ascii value is 113, and ctrl-q is 17
// 113 in binary is 0111 0001 : 0x71
// 17  in binary is 0001 0001, which is a bit mask of 0x1F
// Macro for CTRL values:
#define CTRL_KEY(k) ((k) & 0x1F)


/*** function prototypes ***/
struct abuf;
void refresh_screen();
void keypress_handler();

/*** data ***/

// editor row
typedef struct erow {
    int size;
    char* buf;
} erow;

struct state {
    int cx, cy; // cursor positions
    int screen_rows;
    int screen_cols;
    int num_rows;
    erow row;
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
    state.num_rows = 0;
    if(get_window_size(&state.screen_rows, &state.screen_cols) == -1) error("get_window_size");
    //printf("\r\n%d by %d\r\n", state.screen_rows, state.screen_cols);
}


/*** file i/o ***/
void editor_open(char* filename){
    FILE* fp = fopen(filename, "r");
    if (!fp) error("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t length;
    length = getline(&line, &linecap, fp); // using getline() because we don't know how much memory to allocate per line
    if (length == -1) error("getline");

    // get rid of any trailing whitespace by reducing length
    while(length > 0 &&
         (line[length-1] == '\n' ||
          line[length-1]=='\r')){
        --length;
    }

    state.row.size = length;
    state.row.buf = malloc(length + 1); // room for null char
    memcpy(state.row.buf, line, length);
    state.row.buf[length] = '\0';
    state.num_rows = 1;

    free(line);
    fclose(fp);
}

/*** input ***/

void move_cursor(char c){
    switch (c) {
        case 'h':
            if(state.cx != 0){
                --state.cx;
            }
            break;
        case 'j':
            if(state.cy != state.screen_rows - 1){
                ++state.cy;
            }
            break;
        case 'k':
            if(state.cy != 0){
                --state.cy;
            }
            break;
        case 'l':
            if(state.cx != state.screen_cols - 1){
                ++state.cx;
            }
            break;
    }
}


// read 1 byte from STDIN, store in address of char c
void keypress_handler(){
    char c;
    if(read(STDIN_FILENO, &c, 1) == -1) error("read");

    switch(c){
        case CTRL_KEY('c'):
            //clear_screen();
            exit(0);
            break;
        case 'h':
        case 'j':
        case 'k':
        case 'l':
            move_cursor(c);
            // TODO: delete key
            break;
        default:
            printf("%d ('%c')\r\n", c, c);
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
void editor_draw_rows(struct abuf* ab){
    int i;
    for(i=0;i<state.screen_rows; ++i){
        if(i >= state.num_rows){
            // check if we are outside the range of the currently edited number of rows
            ab_append(ab, "~", 1);
        }else{
            int len = state.row.size;
            if(len > state.screen_cols) len = state.screen_cols;
            ab_append(ab, state.row.buf, len);
        }
        ab_append(ab, "\x1b[K", 3); // erase to the right of current line
        if(i < state.screen_rows - 1){
            ab_append(ab, "\r\n", 2); // dont do newline at bottom
        }
    }
}

void refresh_screen(){
    struct abuf ab = ABUF_INIT;

    ab_append(&ab, "\x1b[?25l", 6); // hide cursor
    ab_append(&ab, "\x1b[H", 3); // reposition cursor to top of screen
    //clear_screen(&ab);

    editor_draw_rows(&ab);

    // the terminal uses 1-indexing, so (1,1) is the top left corner
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", state.cy + 1, state.cx + 1);
    // append this escape sequence, so that we move cursor to the designated location
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
        refresh_screen();
        keypress_handler();
    }

    return 0;
}
