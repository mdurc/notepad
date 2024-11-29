
#include "editor.h"

struct state state;

/* ----------------------------------------- utility -------------------------------------------*/
// error message utility function for exiting with error code and printing error
void error(const char* s){
    perror(s);
    exit(1); // indicate failure with non-zero
}

/* --------------------------------------- program init --------------------------------------- */
int main(int argc, char** argv){
    enable_raw();
    init_editor();
    if(argc >= 2){
        editor_open(argv[1]);
    }

    editor_set_status_msg("movement: vim");

    while (1){
        editor_refresh_screen();
        editor_keypress_handler();
    }

    return 0;
}
