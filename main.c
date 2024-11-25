

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
    attr.c_lflag &= ~(ECHO | ICANON);

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
    while (read(STDIN_FILENO, &c, 1) == 1 && c!='q'){
        // iscntrl are types like tab or newline, or unprintable characters
        if(iscntrl(c)){
            printf("%d\n", c);
        }else{
            // print as ascii value and then as character
            printf("%d ('%c')\n", c, c);
        }
    }

    return 0;
}
