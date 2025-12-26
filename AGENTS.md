### Documentation and specifications for sensors and modules used in the project
F900 Face Recognition Module Documentation - `docs/F900-1.md` and `docs/F900-2.md`
R502 Fingerprint Sensor Documentation - `docs/R502.md`

---

### ESP-IDF Component Creation Guide

If you want to add a new component to the ESP-IDF project, follow these steps:

1. Create a new directory for your component under `components/`.
    - The directory name should be descriptive and follow the kebab-case convention (e.g., `my_new_component`).
2. Add the necessary source files (`.c`, `.h`, etc.) to your component directory.
    - header file should placed in `include/` subdirectory.
    - source files should be placed in component root directory.
    - If your component has multiple source files, you can organize them in subdirectories as needed
3. Update the `CMakeLists.txt` file in your component directory to include your source files.
4. Modify the main application `CMakeLists.txt` file to include your new component.
5. Ensure that your component's header files are included in the main application (`main/main.c`) or other components that need to use it.
6. Implement the required functionality in your component.


---
### Platform Rule: Target MCU and SDK

**Description:**

This project is designed specifically for the **ESP32-S3FH4R2** microcontroller. All developmen must be aligned with this target hardware and software platform.

**Details:**

* **Target MCU:** `ESP32-S3FH4R2`

  * 2.4 GHz Wi-Fi
  * Bluetooth Low Energy (LE)
  * 4MB Flash
  * 2MB PSRAM
  * 27 GPIOs

* **SDK:** The project uses **ESP-IDF v5**, which is the latest stable version at the time of writing.
  All components and modules must be compatible with this version of ESP-IDF.

**Guidelines:**

* Do **not** use features or APIs that are not supported by ESP-IDF v5.
* Do **not** assume more memory or IO capabilities than what the ESP32-S3FH4R2 provides.
* Ensure all peripheral configurations, memory usage, and timing constraints are optimized for this specific chip.

---
### Frontend Rule: No External Dependencies

**Description:**

When developing frontend components (HTML, CSS, JS) for this project, **do not use any external links to CSS or JavaScript frameworks or libraries**. All code must be **self-contained** and written using **modern, standard-compliant JavaScript and CSS**.

**Requirements:**

* Use only **vanilla JavaScript (ES6+)**.
* Use only **standard CSS (Flexbox, Grid, media queries, etc.)**.
* Do **not** include external scripts or styles via `<script src="...">` or `<link href="...">`.
* Do **not** use frameworks or libraries such as:
  - Bootstrap
  - jQuery
  - Tailwind
  - Vue.js / React / Angular
  - Font Awesome
  - Any CDN-hosted resources

**Rationale:**

* Ensures full **offline availability**.
* Minimizes **flash size and runtime dependencies**.

---
### Static File Rule: Serving Web Assets via Built-in HTTP Server

**Description:**

This project includes a built-in HTTP server based on **ESP-IDF**. It is configured to serve static files (HTML, CSS, JS) automatically when no other registered HTTP handler matches a request. Static content must be stored in a designated directory and follows specific processing rules during build.

**Requirements:**

* All static files **must be placed** in:
  `components/static/files/`

* At build time, all static files in this directory will be:

  * **Minified** automatically (to reduce size).
  * **Embedded** into the firmware binary.
  * **Served** automatically by the fallback static file handler.

* The URL path for accessing a static file is directly mapped to the file path.
  For example:
  `components/static/files/test.html` → accessible at `http://<device_ip>/test.html`

**Rules:**

* Place all frontend files (e.g., `.html`, `.css`, `.js`) in `components/static/files/`
* Use only lowercase file names with no spaces
* Do **not** place static files in other directories (e.g., `main/`, `build/`, etc.)
* Do **not** register handlers for static files manually — the fallback logic will handle them automatically

**Example:**

A file created at:
`components/static/files/config.js`
will be accessible at:
`http://<device_ip>/config.js`

---
**Rule: Header Guard Convention**

When creating a new C header file, **do not** use `#pragma once`.

Instead, always use traditional include guards with the following format:

* Use `#ifndef`, `#define`, and `#endif` preprocessor directives.
* The macro name must be all uppercase.
* The macro name must start and end with a single underscore.
* The macro name should be derived from the file name (e.g., `table_types.h` → `_TABLE_TYPES_H_`).

**Example:**

```c
#ifndef _TABLE_TYPES_H_
#define _TABLE_TYPES_H_

// your declarations here

#endif // _TABLE_TYPES_H_
```


## Do not use #ifdef __cplusplus in header files.
Example:

```c
#ifdef __cplusplus
extern "C" {
#endif
```

---
**Rule: Consistent HTTP Handler Organization**

When adding a new HTTP handler (e.g., `/todo`) for API resources or some other operation, ensure it does not logically belong to any existing handler files in the `main/` directory, which follow the naming pattern `web_<xxx>_handlers.c`.

* If your new endpoint logically fits within an existing handler file (`web_<xxx>_handlers.c`):

  * Implement the handler function inside this existing file.
  * Register your endpoint by adding it into the existing registration function `register_<xxx>_web_handlers`.

**Example for existing handler file (`web_photo_handlers.c`):**

```c
static esp_err_t get_photo(httpd_req_t *req) {
    // Handler implementation
    return ESP_OK;
}

void register_photo_web_handlers(httpd_handle_t server) {
    const webserver_uri_t photo_handlers[] = {
        {.uri = "/api/photo", .method = HTTP_GET, .handler = get_photo, .require_auth = true},
    };

    for (int i = 0; i < sizeof(photo_handlers)/sizeof(photo_handlers[0]); i++) {
        webserver_register_uri_handler(server, &photo_handlers[i]);
    }
}
```

* If your new set of endpoints logically forms a distinct group that does not fit into any existing handler files:

  * Create a new handler file named according to the pattern: `web_<xxx>_handlers.c` within the `main/` directory.
  * Structure the new file similarly to the provided example, defining handler functions and a corresponding registration function.
    use `register_<xxx>_web_handlers` for the registration function and organize endpoints like in existing examples.
  * Add your new file to the `CMakeLists.txt` under the `main/` directory to ensure it is compiled and linked correctly.
  * Add your registration function to `main\include\web_handlers.h` file and call this function in `main/main.c` `start_and_configure_webserver()` function.

**New file example (`web_todo_handlers.c`):**

```c
static esp_err_t get_todo(httpd_req_t *req) {
    // Handler implementation
    return ESP_OK;
}

void register_todo_web_handlers(httpd_handle_t server) {
    const webserver_uri_t todo_handlers[] = {
        {.uri = "/api/todo", .method = HTTP_GET, .handler = get_todo, .require_auth = true},
    };

    for (int i = 0; i < sizeof(todo_handlers)/sizeof(todo_handlers[0]); i++) {
        webserver_register_uri_handler(server, &todo_handlers[i]);
    }
}
```

**Always maintain consistency** in endpoint organization and naming conventions.

---
### Rule: Adding Web-Configurable Device Settings

Settings page is located at `/settings` and allows users to configure various device parameters through a web interface (UI).

If you need to add a new setting that can be configured via the web interface, follow **all** of the steps below to ensure consistency and maintainability:

1. **Define the Setting Name and Description**
   Define a unique and descriptive name for the setting, and declare it appropriately in the header file:
   `components/settings/include/settings.h`

   **Do not change the organizational structure of the file.** Place the new setting declaration in the corresponding section that matches its type or purpose.

2. **Implement the Setting Logic**
   Add the implementation and default behavior of the setting in:
   `components/settings/settings.c`

   Follow the existing logic and structure of the file. Group the setting with similar entries and ensure it is correctly integrated into the settings loading, storing, and default initialization mechanisms.

3. **Expose the Setting in the Web Interface**
   Add the setting to the `settingsConfig` object in the file:
   `components/static/files/app.js`

   This step is required to make the setting visible and editable from the web UI. Match the style and format of existing entries.


