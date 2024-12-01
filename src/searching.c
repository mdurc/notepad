
#include "editor.h"

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
    }else if(key == CTRL_KEY('n')){
        // down
        direction = 1;
    }else if(key == CTRL_KEY('N')){
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
