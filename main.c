
/*** includes ***/
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
void refresh_screen();
void clear_screen();

/*** data ***/
struct state {
    struct termios term_defaults;
    int screen_rows;
    int screen_cols;
} state;


/*** terminal data ***/
int get_window_size(int* rows, int* cols){
    struct winsize ws; // from sys/ioctl.h
    // TIOCGWINSZ: Terminal IOCtl Get WINdow SiZe
    // IOCtl: Input/Output Control
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

/*** terminal setup **/
void error(const char* s){
    clear_screen();
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

void init_window(){
    if(get_window_size(&state.screen_rows, &state.screen_cols) == -1) error("get_window_size");
    //printf("%d by %d\r\n", state.screen_rows, state.screen_cols);
}

/*** input ***/
// read 1 byte from STDIN, store in address of char c
void keypress_handler(){
    char c;
    if(read(STDIN_FILENO, &c, 1) == -1) error("read");

    switch(c){
        case CTRL_KEY('c'):
            clear_screen();
            exit(0);
            break;
        default:
            printf("%d ('%c')\r\n", c, c);
            break;
    }
}

/*** output ***/
void draw_rows(){
    int i;
    for(i=0;i<state.screen_rows; ++i){
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void clear_screen(){
    /* write 4 bytes to stdout
       - 1 byte : \x1b: the escape character, 27 in decimal
       - 3 bytes: [2J : J is erase in display, and the argument (2) says to clear the
       entire screen ([1J) would clear up to the cursor
   */
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor to top of screen
    // for coordinates: <esc>[12;40H, arguments separated by a colon
}

void refresh_screen(){
    clear_screen();
    draw_rows();
    write(STDOUT_FILENO, "\x1b[H", 3); // reposition cursor to top of screen
}


/*** program init ***/
int main(){
    enable_raw();
    init_window();

    while (1){
        refresh_screen();
        keypress_handler();
    }

    return 0;
}
