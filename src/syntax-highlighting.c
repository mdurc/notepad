

#include "editor.h"

// secondary keywords are marked by ending with a '|'
// ends in NULL so that we can loop through items without knowing the size of array
char* C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};


/* ------------------------------------- syntax highlighting ----------------------------------- */
// moves through erow's render array and hl array, assigning highlight descriptions for each char within render, in hl
// in charge of filling hl array
void editor_update_syntax(erow* row){
    // hl points to type uint8_t which is an unsigned char, which is 1 byte
    row->hl = realloc(row->hl, row->rsize /* * sizeof(unsigned char) */);
    memset(row->hl, HL_NORMAL, row->rsize);

    int prev_sep = 1; // start of row should act as a valid separator
    int in_string = 0; // flag for inside double or single quotes.
                       // it will equal the double or single quote so we can highlight: "jack's"
    int in_comment = (row->idx > 0 && state.row[row->idx - 1].hl_open_comment);
    int i;
    for(i=0;i<row->rsize;++i){
        char c = row->render[i];
        uint8_t prev_hl = (i > 0) ? row->hl[i-1]:HL_NORMAL;

        if(!in_string && !in_comment && !strncmp(&row->render[i], "//", 2)){
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break; // don't try to color anything else, if it is in a comment line
        }

        if (in_comment) {
            row->hl[i] = HL_MLCOMMENT;
            if (!strncmp(&row->render[i], "*/", 2)) {
                memset(&row->hl[i], HL_MLCOMMENT, 2);
                ++i;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                continue;
            }
        } else if (!strncmp(&row->render[i], "/*", 2)) {
            memset(&row->hl[i], HL_MLCOMMENT, 2);
            ++i;
            in_comment = 1;
            continue;
        }

        if(in_string){
            row->hl[i] = HL_STRING;
            if(c == '\\' && i+1 < row->rsize){
                // If there is a backslash, move to the next character
                // then color the next character as well.

                // Accounts for "hi\"there"
                row->hl[i+1] = HL_STRING;
                ++i;
                continue;
            }
            if(c == in_string) in_string = 0;
            prev_sep = 1; // ended string highlighting
            continue;
        }else{
            if(c == '"' || c == '\''){
                in_string = c;
                row->hl[i] = HL_STRING;
                continue;
            }
        }

        if(isdigit(c) && ((prev_sep || prev_hl == HL_NUMBER) ||
                          (c == '.' && prev_hl == HL_NUMBER))){
            row->hl[i] = HL_NUMBER;
            prev_sep = 0; // we are currently highlighting the number
            continue;
        }

        if(prev_sep){
            int j;
            for(j=0; C_HL_keywords[j]; ++j){
                int keyword_len = strlen(C_HL_keywords[j]);
                int is_kw2 = C_HL_keywords[j][keyword_len - 1] == '|';
                if (is_kw2) --keyword_len;

                if (!strncmp(&row->render[i], C_HL_keywords[j], keyword_len) &&
                        is_separator(row->render[i + keyword_len])) {
                    memset(&row->hl[i], is_kw2 ? HL_KEYWORD2 : HL_KEYWORD1, keyword_len);
                    i += (keyword_len-1); // outer loop increments
                    break;
                }
            }
            if (C_HL_keywords[j] != NULL) {
                // means that we had to break out of previous loop
                prev_sep = 0; // just ended on a keyword
                continue;
            }
        }

        prev_sep = is_separator(c);
    }

    // check if we closed the multi-line comment or not
    int changed = (row->hl_open_comment != in_comment);
    row->hl_open_comment = in_comment;
    if(changed && row->idx + 1 < state.num_rows){
        editor_update_syntax(&state.row[row->idx + 1]);
    }
}

// returns the ansi code for integers from editor_highlight
int editor_syntax_to_color(uint8_t hl){
    switch(hl){
        case HL_MLCOMMENT:
        case HL_COMMENT: return 36; // cyan
        case HL_KEYWORD1: return 33; // yellow
        case HL_KEYWORD2: return 32; // green
        case HL_STRING: return 35; // magenta
        case HL_NUMBER: return 31; // forground red
        case HL_MATCH: return 34; // bright blue
        default: return 37; // foreground white
    }
}

// for identifying when a number is isolated vs within a string
int is_separator(int c){
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:\"\'", c) != NULL;
}
