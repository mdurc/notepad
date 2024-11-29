
#include <stdio.h>
#include "editor.h"

void read_normal_mode(int c){
    char second_char;
    int bytes_read;

    // check if it is a relative jump
    if(isdigit(c)){
        if(read(STDIN_FILENO, &second_char, 1) != 1) error("read");
        c = c - '0';
        while(c--){
            move_cursor(second_char);
        }
        return;
    }

    switch(c){
        case ':':
            read_command_mode();
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
        case 'g':
            if(read(STDIN_FILENO, &second_char, 1) == 1 && second_char == 'g'){
                state.cy = 0;
            }
            break;
        case '/':
            editor_find();
            break;
        case BACKSPACE:
            move_cursor('h');
            break;
        case '\r':
            move_cursor('j');
            break;
        case 'I':
            state.cx = 0;
        case 'i':
            state.mode = INSERT_MODE;
            break;
        case 'A':
            if(state.cy < state.num_rows){
                state.cx = state.row[state.cy].size;
            }
            state.mode = INSERT_MODE;
            break;
        case 'a':
            ++state.cx;
            state.mode = INSERT_MODE;
            break;
        case 'O':
            state.cx = 0;
            editor_insert_newline();
            --state.cy;
            state.mode = INSERT_MODE;
            break;
        case 'o':
            if(state.cy < state.num_rows){
                state.cx = state.row[state.cy].size;
            }
            editor_insert_newline();
            state.mode = INSERT_MODE;
            break;
        case 'c':
            if(read(STDIN_FILENO, &second_char, 1) == 1 && second_char == 'w'){
                editor_delete_word();
            }
            state.mode = INSERT_MODE;
            break;
        case 'd':
            bytes_read = read(STDIN_FILENO, &second_char, 1);
            if(bytes_read && second_char == 'w'){
                editor_delete_word();
            }else if(bytes_read && second_char == 'd'){
                state.cx = 0;
                editor_delete_row(state.cy);
            }
            break;
        case 'x':
            if((state.cx + 1) <= state.row[state.cy].size){
                ++state.cx;
                editor_delete_char();
            }
            break;
        case 'r':
            if(read(STDIN_FILENO, &second_char, 1) == 1){
                if((state.cx + 1) <= state.row[state.cy].size){
                    ++state.cx;
                    editor_delete_char();
                }
                editor_insert_char(second_char);
                if(--state.cx < 0) state.cx = 0;
            }
            break;
        case 'F':
            if(read(STDIN_FILENO, &second_char, 1) == 1){
                backwards_F(second_char);
            }
            break;
        case 'f':
            if(read(STDIN_FILENO, &second_char, 1) == 1){
                forwards_F(second_char);
            }
            break;
        case 'T':
            if(read(STDIN_FILENO, &second_char, 1) == 1){
                backwards_T(second_char);
            }
            break;
        case 't':
            if(read(STDIN_FILENO, &second_char, 1) == 1){
                forwards_T(second_char);
            }
            break;
        case 'w':
        case 'e':
        case 'b':
        case 'h':
        case 'j':
        case 'k':
        case 'l':
            move_cursor(c);
            break;
        default: break;
    }
}

void read_insert_mode(int c){
    switch(c){
    case BACKSPACE:
        editor_delete_char();
        break;
    case '\r': // enter
        editor_insert_newline();
        break;
    case '\t': // tab
        editor_insert_char(c);
        break;
    default:
        if(!iscntrl(c)) editor_insert_char(c);
    }
}

void read_visual_mode(int c){
    fprintf(stderr, "Visual keybind %c (%c)\n", c,c );
}

void read_command_mode(){
    char* query = editor_prompt(":%s", NULL);
    if(strlen(query) == 1 && (*query == 'w' || *query == 'W')){
        editor_save();
    }
    if(query) free(query);
}


// Moving cursor on non-insertion/deletion of characters. Purely movement characters.
void move_cursor(int c){
    erow* row = (state.cy >= state.num_rows) ? NULL : &state.row[state.cy];
    if(!row) error("moving on non-row"); // I have it setup so that row should never be NULL
    switch (c) {
        case 'h':
            if(state.cx != 0){
                --state.cx;
            }else if(state.cy > 0){
                --state.cy;
                state.cx = state.row[state.cy].size;
            }
            break;
        case 'j':
            // num rows is the amount of rows in the current file being viewed
            // so if there is no file, we cannot move down
            if(state.cy < (state.num_rows-1)){
                ++state.cy;
            }
            break;
        case 'k':
            if(state.cy != 0){
                --state.cy;
            }
            break;
        case 'l':
            if(state.cx < row->size){
                ++state.cx;
            }else if(state.cy < (state.num_rows-1)){
                ++state.cy;
                state.cx = 0;
            }
            break;
        case 'e':
            move_end_next_word();
            break;
        case 'w':
            move_next_word();
            break;
        case 'b':
            move_previous_word();
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

void move_end_next_word() {
    char* p = state.row[state.cy].chars;
    int i = state.cx;
    int size = state.row[state.cy].size;

    if (i >= size) return;

    // skip initial whitespace
    while (i < size && isspace(p[i])) ++i;

    // move to end of next current word
    ++i;
    while (i < size && !is_separator(p[i]) && !isspace(p[i])) ++i;

    // move back to the last character
    if (i > 0 && (isspace(p[i]) || is_separator(p[i]))) --i;

    if(i == state.cx){
        ++i;
        while (i < size && isspace(p[i])) ++i;
        while (i < size && !is_separator(p[i]) && !isspace(p[i])) ++i;
        if(i < size && isspace(p[i])) --i;
        if(i >= 0){
            state.cx = i;
        }
    }else{
        state.cx = i;
    }
}

void move_next_word(){
    // search for the first character after the nearest space
    char* p = state.row[state.cy].chars;
    int i = state.cx;
    int size = state.row[state.cy].size;
    ++i; // move forward one space
    while(i < size && !is_separator(p[i])) ++i;
    while(i < size && isspace(p[i])) ++i;
    if(i < size){
        state.cx = i;
    }
}

void move_previous_word(){
    char* p = state.row[state.cy].chars;
    int i = state.cx;
    // get off of starting whitespace
    while(i >= 0 && is_separator(p[i])) --i;
    while((i-1) >= 0 && is_separator(p[i-1])) i-=2;

    while(!is_separator(p[i]) && (--i) >= 0);
    ++i;
    if(i >= 0){
        state.cx = i;
    }
}

void backwards_F(int c) {
    char* p = state.row[state.cy].chars;
    int i = state.cx;

    while (i >= 0) {
        if (p[i] == c) {
            state.cx = i;
            return;
        }
        --i;
    }
}

void forwards_F(int c) {
    char* p = state.row[state.cy].chars;
    int i = state.cx + 1;
    int size = state.row[state.cy].size;

    while (i < size) {
        if (p[i] == c) {
            state.cx = i;
            return;
        }
        ++i;
    }
}

void backwards_T(int c) {
    char* p = state.row[state.cy].chars;
    int i = state.cx;

    while (i > 0) {
        if (p[i - 1] == c) {
            state.cx = i - 1;
            return;
        }
        --i;
    }
}

void forwards_T(int c) {
    char* p = state.row[state.cy].chars;
    int i = state.cx + 1;
    int size = state.row[state.cy].size;

    while (i < size) {
        if (p[i] == c) {
            state.cx = i - 1;
            return;
        }
        ++i;
    }
}
