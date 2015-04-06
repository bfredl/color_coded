
typedef struct highlight_t {
    size_t line;
    size_t col;
    size_t length;
    const char* group;
} highlight_t;

typedef void (*color_coded_callback_T)(int buf, highlight_t hl[], size_t n_hl);

// request that buffer buf should be highligted according to (file, data)
// callback will be invoked on separate thread
// *might* supercede earlier request on buf
int color_coded_request(color_coded_callback_T callback, int buf, char* file, char* data);
