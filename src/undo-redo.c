
#include "editor.h"

// deep copy of a row (for undo-redo)
stack_entry editor_deep_copy_row(const erow* src, int action) {
    stack_entry copy;

    copy.row.idx = src->idx;
    copy.row.hl_open_comment = src->hl_open_comment;
    copy.row.size = src->size;
    copy.row.rsize = src->rsize;

    copy.row.chars = malloc(copy.row.size + 1);
    memcpy(copy.row.chars, src->chars, copy.row.size);
    copy.row.chars[copy.row.size] = '\0';

    copy.row.render = malloc(copy.row.rsize + 1);
    memcpy(copy.row.render, src->render, copy.row.rsize);
    copy.row.render[copy.row.rsize] = '\0';

    copy.row.hl = malloc(copy.row.rsize);
    memcpy(copy.row.hl, src->hl, copy.row.rsize);

    copy.cx = state.cx;
    copy.cy = state.cy;
    copy.action = action;

    return copy;
}

erow editor_copy_row(const erow* src) {
    erow copy;
    copy.idx = src->idx;
    copy.hl_open_comment = src->hl_open_comment;
    copy.size = src->size;
    copy.rsize = src->rsize;

    copy.chars = malloc(copy.size + 1);
    memcpy(copy.chars, src->chars, copy.size);
    copy.chars[copy.size] = '\0';

    copy.render = malloc(copy.rsize + 1);
    memcpy(copy.render, src->render, copy.rsize);
    copy.render[copy.rsize] = '\0';

    copy.hl = malloc(copy.rsize);
    memcpy(copy.hl, src->hl, copy.rsize);

    return copy;
}

void editor_split_row(int row_idx) {
    if (row_idx < 0 || row_idx >= state.num_rows) return;

    erow* row = &state.row[row_idx];
    // split at the end of the original row before merging
    int split_point = row->size - strlen(state.undo.saves[state.undo.stack_size-1].row.chars);

    editor_insert_row(row_idx + 1, row->chars + split_point, row->size - split_point);

    // truncate current row
    row->size = split_point;
    row->chars[split_point] = '\0';

    editor_update_row(row);
}

void editor_merge_row_below(int row_idx) {
    if (row_idx < 0 || row_idx+1 >= state.num_rows) return;

    erow* curr = &state.row[row_idx];
    erow* prev = &state.row[row_idx + 1];

    editor_row_append_string(curr, prev->chars, prev->size);
    editor_delete_row(row_idx+1);
}

// push a row state to undo/redo stacks
void editor_push_to_stack(struct stack* s, erow* row, int action) {
    if (s->stack_size >= s->mem_size) {
        s->mem_size = (s->stack_size + 1) * 2;
        s->saves = realloc(s->saves, sizeof(stack_entry) * s->mem_size);
    }

    // push a deep copy of the row onto the stack
    s->saves[s->stack_size] = editor_deep_copy_row(row, action);
    ++s->stack_size;
}

// pop the last state from the undo stack and revert the row
void editor_undo() {
    if (state.undo.stack_size == 0) {
        editor_set_status_msg("Nothing to undo.");
        return;
    }

    stack_entry* undo_entry = &state.undo.saves[state.undo.stack_size - 1];
    erow redo_row = editor_copy_row(&state.row[state.cy]);
    int redo_action = -1;

    state.undoing = 1;
    switch (undo_entry->action) { // the action that the user wants to undo
    case MODIFY_ROW:
        redo_action = MODIFY_ROW;
        if (undo_entry->row.idx < state.num_rows) {
            editor_free_row(&state.row[undo_entry->row.idx]);
            state.row[undo_entry->row.idx] = editor_copy_row(&undo_entry->row);
        }
        break;
    case DELETE_ROW:
        //redo_action = DELETE_ROW; // THESE ARE NOT WORKING YET
        editor_insert_row(undo_entry->row.idx, undo_entry->row.chars, undo_entry->row.size);
        break;
    case MERGE_ROW_UP:
        //redo_action = MERGE_ROW_UP;
        editor_split_row(undo_entry->row.idx);
        break;
    case SPLIT_ROW_DOWN:
        //redo_action = SPLIT_ROW_DOWN;
        editor_merge_row_below(undo_entry->row.idx);
        break;
    case NEWLINE_ABOVE:
        //redo_action = NEWLINE_ABOVE;
        editor_delete_row(undo_entry->row.idx);
        break;
    }

    if(redo_action != -1){
        editor_push_to_stack(&state.redo, &redo_row, redo_action);
    }

    // restore cursor position
    state.cx = undo_entry->cx;
    state.cy = undo_entry->cy;

    editor_free_stack_entry(undo_entry);
    --state.undo.stack_size;
    editor_set_status_msg("Undo successful.");
    state.dirty = 1;
    state.undoing = 0;

}

// TODO: maybe merge this into one function with editor_undo
void editor_redo() {
    if (state.redo.stack_size == 0) {
        editor_set_status_msg("Nothing to redo.");
        return;
    }
    state.undoing = 1;

    struct stack_entry* redo_entry = &state.redo.saves[state.redo.stack_size-1];
    erow undo_row = editor_copy_row(&state.row[redo_entry->row.idx]);
    int undo_action = -1;

    // Apply redo action
    switch (redo_entry->action) { // the action that we are performing
        // TODO: as of now, only one working redo operation
        case MODIFY_ROW:
            undo_action = MODIFY_ROW;
            if (redo_entry->row.idx < state.num_rows) {
                editor_free_row(&state.row[redo_entry->row.idx]);
                state.row[redo_entry->row.idx] = editor_copy_row(&redo_entry->row);
            }
            break;
    }

    if(undo_action != -1){
        editor_push_to_stack(&state.undo, &undo_row, undo_action);
    }

    state.cx = redo_entry->cx;
    state.cy = redo_entry->cy;

    editor_free_stack_entry(redo_entry);
    --state.redo.stack_size;
    editor_set_status_msg("Redo successful.");
    state.dirty = 1;
    state.undoing = 0;
}

// initialize the undo/redo stacks
void editor_init_undo_redo_stacks() {
    state.undo.stack_size = 0;
    state.undo.mem_size = 16;
    state.undo.saves = malloc(sizeof(stack_entry) * state.undo.mem_size);

    state.redo.stack_size = 0;
    state.redo.mem_size = 16;
    state.redo.saves = malloc(sizeof(stack_entry) * state.redo.mem_size);
}

void editor_free_stack_entry(stack_entry* entry) {
    if(entry == NULL) return;
    if(entry->row.chars) free(entry->row.chars);
    if(entry->row.render) free(entry->row.render);
    if(entry->row.hl) free(entry->row.hl);
}

// free memory of the stack
void editor_free_stack(struct stack* s) {
    if(s==NULL) return;
    for (int i = 0; i < s->stack_size; ++i) {
        editor_free_stack_entry(&s->saves[i]);
    }
    s->stack_size = 0;
    s->mem_size = 0;
}
