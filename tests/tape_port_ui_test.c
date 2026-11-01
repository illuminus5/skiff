#define UI_PORT_ONLY 1
#include "ui_test.c"

int main(void){setvbuf(stdout,0,_IONBF,0);int bad=0;bad+=test_tape_aux_page();bad+=test_tape_fx_pages();bad+=test_menu_tabs();bad+=test_u4();bad+=test_menu_prefs();printf("V2 tape UI: %d failures\n",bad);return !!bad;}
