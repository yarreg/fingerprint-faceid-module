#include <stdint.h>
#include <stddef.h>
#include <string.h>



typedef struct {
    const char *fname;
    const char *data_start;
    const char *data_end;
} static_file_t;

// Embedded files
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char camera_html_start[] asm("_binary_camera_html_start");
extern const char camera_html_end[] asm("_binary_camera_html_end");
extern const char fingerprint_html_start[] asm("_binary_fingerprint_html_start");
extern const char fingerprint_html_end[] asm("_binary_fingerprint_html_end");
extern const char face_html_start[] asm("_binary_face_html_start");
extern const char face_html_end[] asm("_binary_face_html_end");
extern const char settings_html_start[] asm("_binary_settings_html_start");
extern const char settings_html_end[] asm("_binary_settings_html_end");
extern const char update_html_start[] asm("_binary_update_html_start");
extern const char update_html_end[] asm("_binary_update_html_end");
extern const char about_html_start[] asm("_binary_about_html_start");
extern const char about_html_end[] asm("_binary_about_html_end");
extern const char log_html_start[] asm("_binary_log_html_start");
extern const char log_html_end[] asm("_binary_log_html_end");

const static_file_t files[] = {
    {"index.html", index_html_start, index_html_end},
    {"style.css", style_css_start, style_css_end},
    {"app.js", app_js_start, app_js_end},
    {"camera.html", camera_html_start, camera_html_end},
    {"fingerprint.html", fingerprint_html_start, fingerprint_html_end},
    {"face.html", face_html_start, face_html_end},
    {"settings.html", settings_html_start, settings_html_end},
    {"update.html", update_html_start, update_html_end},
    {"about.html", about_html_start, about_html_end},
    {"log.html", log_html_start, log_html_end},
    {NULL, NULL, NULL}
};

const char *get_static_file(const char *fname, size_t *size) {
    for (const static_file_t *file = files; file->fname; file++) {
        if (strcmp(file->fname, fname) == 0) {
            // File found
            *size = file->data_end - file->data_start;
            if (*size > 0 && file->data_start[*size - 1] == '\0') {
                (*size)--;
            }
            return file->data_start;
        }
    }
    return NULL;
}

const char *get_static_text_file(const char *fname) {
    size_t size;
    const char *text = get_static_file(fname, &size);

    // if text is not \0 terminated it looks like a binary file
    // and we should not return NULL because it is not safe to use it as a string
    if (text && text[size - 1] == '\0') {
        return text;
    }

    return NULL;
}
