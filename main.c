

#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>

struct termios term_defaults;
void disable_raw(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_defaults);
    printf("Disabling raw mode\n");
}

// disables flags for cannonical mode
// so we can read bytes as they are entered in stdin
// and don't have to wait for "enter"
void enable_raw(){
    tcgetattr(STDIN_FILENO, &term_defaults);
    atexit(disable_raw); // accepts a function ptr void (*) (void)

    // make a struct to retrieve the attributes of terminal
    struct termios attr = term_defaults;
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

    // ----
    // by default, VTIME is 0 and VMIN is 1, so it blocks processes until an input is registered
    // If we want to simulate some animation, we can use the following, to create a ticking effect

    // Set control characters (array of bytes)
    // nbytes specied by read() is the amount of data we hope to get, VMIN is the amount we will settle for.
    attr.c_cc[VMIN] = 0;

    // specifies the time to read "instantaneous" series of inputs before processing it as
    // a combined input data. Processed in tenths of a second. so this is set to 1/10 of a second, or 100 milliseconds.
    attr.c_cc[VTIME] = 1; // this requires us to rewrite main to process indefinitely


    // set these changes by flushing stdin and then setting the changes
    // if there is leftover input, it will be flushed and wont be fed into the terminal as a bunch of commands
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &attr);
}



int main(){
    enable_raw();
    char c;
    // read 1 byte from STDIN, store in address of char c
    // - Issue is that the terminal starts in cannonical mode, so the "reading" only ever occurs once the user presses "Enter"
    // "stdin" is a FILE pointer, while STDIN_FILENO is a number
    //#define	 STDIN_FILENO	0	/* standard input file descriptor */
    //STDOUT is 1, and STDERR is 2. Just as in linux with commands like: ./notes 2> err.txt
    while (1){
        c = '\0';
        read(STDIN_FILENO, &c, 1);
        // iscntrl are types like tab or newline, or unprintable characters
        if(iscntrl(c)){
            // note that some of these, like arrow-keys, are escape sequences, all starting with a "27" byte and containing 2 more bytes.
            // Ex. if you just press 'esc', then it will output "27" alone.
            // Ex. 'Enter' is 10
            printf("%d\r\n", c);
        }else{
            // print as ascii value and then as character
            printf("%d ('%c')\r\n", c, c);
        }
        if(c == 'q') break;
    }

    return 0;
}
