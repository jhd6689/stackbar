

#include <pango/pangocairo.h>
#include "main.h"
#include "app_menu.h"
#include "application.h"
#include "audio.h"
#include "bind_meta.h"
#include "root.h"
#include "systray.h"
#include "taskbar.h"
#include "config.h"
#include "globals.h"
#include "notifications.h"
#include "wifi_backend.h"
#include "simple_dbus.h"
#include "icons.h"

App *app;

bool restart = false;

void check_config_version();

int main() {
//    char buf[102];
//    buf[0] = '\0';
//    int len = strlen(buf);
//    std::string test = std::string(buf, -1);
    
    global = new globals;
    
    // Open connection to app
    app = app_new();
    
    if (app == nullptr) {
        printf("Couldn't start application\n");
        return -1;
    }
    
    // Load the config
    config_load();
    config->taskbar_height = config->taskbar_height * config->dpi;
    
    check_config_version();
    
    active_tab = config->starting_tab_index == 0 ? "Apps" : "Scripts";
    
    load_icons(app);
    
    // Add listeners and grabs on the root window
    root_start(app);
    
    // Start the pulseaudio connection
    audio_start(app);
    
    // We need to register as the systray
    register_as_systray();
    
    // Open our windows
    AppClient *taskbar = create_taskbar(app);
    
    // We only want to load the desktop files once at the start of the program
    //std::thread(load_desktop_files).detach();
    load_all_desktop_files();
    load_scripts();// The scripts are reloaded every time the search_menu window closes
    load_historic_scripts();
    load_historic_apps();
    
    client_show(app, taskbar);
    xcb_set_input_focus(app->connection, XCB_INPUT_FOCUS_PARENT, taskbar->window, XCB_CURRENT_TIME);
    
    on_meta_key_pressed = meta_pressed;
    
    dbus_start();
    
    wifi_start(app);
    
    // Start our listening loop until the end of the program
    app_main(app);
    
    unload_icons();
    
    dbus_end();
    
    // Clean up
    app_clean(app);
    
    audio_stop();
    
    wifi_stop();
    
    for (auto l: launchers) {
        delete l;
    }
    launchers.clear();
    launchers.shrink_to_fit();
    
    delete global;
    
    if (restart) {
        restart = false;
        main();
    }
    
    return 0;
}

static int acceptable_config_version = 2;

std::string first_message;
std::string second_message;
std::string third_message;

void paint_wrong_version(AppClient *client, cairo_t *cr, Container *container) {
    set_rect(cr, container->real_bounds);
    set_argb(cr, correct_opaqueness(client, config->color_volume_background));
    cairo_fill(cr);
    
    PangoLayout *layout = get_cached_pango_font(cr, config->font, 14, PangoWeight::PANGO_WEIGHT_BOLD);
    int width;
    int height;
    pango_layout_set_text(layout, first_message.data(), first_message.size());
    pango_layout_get_pixel_size(layout, &width, &height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  (int) (container->real_bounds.x + container->real_bounds.w / 2 - width / 2),
                  10);
    pango_cairo_show_layout(cr, layout);
    
    
    layout = get_cached_pango_font(cr, config->font, 12, PangoWeight::PANGO_WEIGHT_NORMAL);
    int second_height;
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - 20) * PANGO_SCALE);
    pango_layout_set_text(layout, second_message.data(), second_message.size());
    pango_layout_get_pixel_size(layout, &width, &second_height);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10,
                  10 + height + 10);
    pango_cairo_show_layout(cr, layout);
    
    pango_layout_set_text(layout, third_message.data(), third_message.size());
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_width(layout, (container->real_bounds.w - 20) * PANGO_SCALE);
    
    set_argb(cr, config->color_volume_text);
    cairo_move_to(cr,
                  10,
                  10 + height + 10 + second_height + 5);
    pango_cairo_show_layout(cr, layout);
}

void check_config_version() {
    if (config->config_version == 0 ||
        config->config_version > acceptable_config_version ||
        config->config_version < acceptable_config_version ||
        !config->found_config) {
        Settings settings;
        settings.w = 400;
        settings.h = 200;
        auto client = client_new(app, settings, "winbar_version_check");
        client->root->when_paint = paint_wrong_version;
        
        first_message = "Couldn't start WinBar";
        char *home = getenv("HOME");
        std::string config_directory(home);
        config_directory += "/.config/winbar/winbar.cfg";
        
        if (!config->found_config) {
            second_message = "We didn't find a Winbar config at: " + config_directory;
            third_message = "To fix this, head over to https://github.com/jmanc3/winbar, "
                            "read the README.md (in particular the section about missing icons), "
                            "and make sure you unzip \"winbar.zip\" into the correct place.";
        } else if (config->config_version == 0) {
            second_message = "We found a config file, but there was no version number in it, which means it is out of date.";
            third_message = "To fix this, head over to https://github.com/jmanc3/winbar, "
                            "download the resources: \"winbar.zip\", "
                            "and unzip them into the correct place (as you've done once already) overriding the old files.";
        } else if (config->config_version > acceptable_config_version) {
            second_message = "The config version is \"";
            second_message += std::to_string(config->config_version);
            second_message += "\", which is too new compared to Winbar's acceptable config version \"";
            second_message += std::to_string(acceptable_config_version);
            second_message += "\".";
            third_message = "To fix this, pull the latest https://github.com/jmanc3/winbar, compile, and install it.";
        } else if (config->config_version < acceptable_config_version) {
            second_message = "The config version is \"";
            second_message += std::to_string(config->config_version);
            second_message += "\", which is too old compared to Winbar's acceptable config version \"";
            second_message += std::to_string(acceptable_config_version);
            second_message += "\".";
            third_message = "To fix this, head over to https://github.com/jmanc3/winbar, "
                            "download the resources: \"winbar.zip\", "
                            "and unzip them into the correct place (as you have already done once) overriding the old files.";
        }
        
        client_layout(app, client);
        request_refresh(app, client);
        client_show(app, client);
        app_main(app);
        app_clean(app);
    }
}