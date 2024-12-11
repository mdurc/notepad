
#include "editor.h"

/* ------------------------------------ editor operations ------------------------------------ */

// initialize editor state
void init_editor(){
    state.mode = NORMAL_MODE;
    state.cx = 0;
    state.cy = 0;
    state.rx = 0;
    state.rowoff = 0;
    state.coloff = 0;
    state.num_rows = 0;
    state.row = NULL;
    state.undoing = 0;
    state.dirty = 0;
    state.filename = NULL;
    state.statusmsg[0] = '\0';
    state.statusmsg_time = 0;
    if(get_window_size(&state.screen_rows, &state.screen_cols) == -1) error("get_window_size");
    state.screen_rows -= 2; // room for status bar and msg

    editor_init_undo_stack();
}

void end_editor(){
    //fprintf(stderr, "Freeing all memory\n");
    for(int i=0;i<state.num_rows;++i){
        editor_free_row(state.row+i);
    }
    if(state.filename != NULL){
        free(state.filename);
        state.filename = NULL;
    }
    editor_free_undo_stack();
}


// adjusts cursor positions of inserting a character
void editor_insert_char(char c){
    // check if the file is empty
    if(state.cy == state.num_rows){
        editor_insert_row(state.num_rows, "", 0);
    }

    if(!state.undoing) editor_push_undo(&state.row[state.cy], MODIFY_ROW);
    editor_row_insert_char(&state.row[state.cy], state.cx, c);
    ++state.cx;
    state.dirty = 1;
}

// handles newline characters by creating a new row, possibly splitting the current row.
// adjusts the cursor positions as well.
void editor_insert_newline(){
    if(state.cx == 0){
        // we are newlining at the start of a line
        if(!state.undoing) editor_push_undo(&state.row[state.cy], NEWLINE_ABOVE);
        editor_insert_row(state.cy, "", 0);
    }else{
        erow *row = &state.row[state.cy];
        if(!state.undoing) editor_push_undo(row, SPLIT_ROW_DOWN);
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
    state.dirty = 1;
}

// Adjusts cursor positions, delegates to row deletion functions
void editor_delete_char(){
    if (state.cy == state.num_rows) return; // on a new empty file
    erow* curr = &state.row[state.cy];
    if(state.cx > 0){
        // technically the cursor deletes the character BEHIND the currently highlighted one
        // If we used 'x' in vim, though, it would delete the CURRENT character at cx.
        if(!state.undoing) editor_push_undo(curr, MODIFY_ROW);
        editor_row_delete_char(curr, state.cx-1);
        --state.cx;
    }else if(state.cy > 0){
        // then we are at the start of the line, and not at the beginning of file

        --curr->idx; // so that when we undo, we arent out of bounds bc we are deleting the current row
        if(!state.undoing) editor_push_undo(curr, MERGE_ROW_UP);
        ++curr->idx;

        state.cx = state.row[state.cy-1].size;
        editor_row_append_string(&state.row[state.cy-1], curr->chars, curr->size);

        // shouldnt count this deletion as an undo, might want to find a better
        // way to do this, but for now im just going to leave it like this
        state.undoing = 1;
        editor_delete_row(state.cy);
        state.undoing = 0;

        --state.cy;
    }
    state.dirty = 1;
}


void editor_delete_word() {
    char* p = state.row[state.cy].chars;
    int i = state.cx;
    int size = state.row[state.cy].size;
    while (i < size && !is_separator(p[i]) && !isspace(p[i])) ++i;

    int num_characters = i-state.cx;
    state.cx = i;

    for(i=0; i<num_characters; ++i){
        editor_delete_char();
    }
}
