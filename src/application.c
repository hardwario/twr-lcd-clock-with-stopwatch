#include <application.h>

// LED instance
twr_led_t led;

// Thermometer instance
twr_tmp112_t tmp112;

// Accelerometer instance
twr_lis2dh12_t lis2dh12;

// Dice instance
twr_dice_t dice;
twr_dice_face_t dice_face = TWR_DICE_FACE_1;

// GFX pointer
twr_gfx_t *pgfx;

// Stopwatch timestamp
uint32_t timestamp;

bool display_seconds = false;
bool display_voltage = false;
bool display_temperature = false;

typedef enum
{
    RTC_CHANGE_HOURS,
    RTC_CHANGE_MINUTES,
} rtc_change_t;

typedef enum
{
    CLOCK_MODE_DISPLAY,
    CLOCK_MODE_SET,
    CLOCK_MODE_STOPWATCH
} clock_mode_t;

#define TEMPERATURE_UPDATE_INTERVAL (2 * 60 * 1000)
#define ACCELEROMETER_UPDATE_INTERVAL (2 * 1000)
#define BATTERY_UPDATE_INTERVAL (5 * 60 * 1000)

float voltage;
float temperature;

// Task called application_task() has ID always 0
#define APPLICATION_TASK_ID 0

clock_mode_t clock_mode = CLOCK_MODE_DISPLAY;
int cursor = 0;

void rtc_change_time(rtc_change_t change, int increment)
{
    struct tm rtc;
    twr_rtc_get_datetime(&rtc);

    switch (change)
    {
        case RTC_CHANGE_HOURS:
        {
            int hours_temp = rtc.tm_hour;
            hours_temp += increment;

            if (hours_temp > 23)
            {
                hours_temp = hours_temp % 24;
            }
            else if (hours_temp < 0)
            {
                hours_temp = 24 + (hours_temp % 24);
            }

            rtc.tm_hour = hours_temp;

            break;
        }

        case RTC_CHANGE_MINUTES:
        {
            int minutes_temp = rtc.tm_min;
            minutes_temp += increment;

            if (minutes_temp > 59)
            {
                minutes_temp = minutes_temp % 60;
            }
            else if (minutes_temp < 0)
            {
                minutes_temp = 60 + (minutes_temp % 60);
            }

            rtc.tm_min = minutes_temp;
            break;
        }

        default:
        {
            return;
        }
    }

    // Set year to non-zero value so RTC init code
    // would not reinitialize RTC after reset
    rtc.tm_year = 1;

    rtc.tm_sec = 0;

    twr_rtc_set_datetime(&rtc, 0);

    twr_scheduler_plan_now(APPLICATION_TASK_ID);
}

void lcd_event_handler(twr_module_lcd_event_t event, void *event_param)
{
    twr_scheduler_plan_relative(APPLICATION_TASK_ID, 10);

    if (event == TWR_MODULE_LCD_EVENT_BOTH_HOLD)
    {
        display_seconds = !display_seconds;
    }

    if (event == TWR_MODULE_LCD_EVENT_LEFT_HOLD)
    {
        if (clock_mode == CLOCK_MODE_DISPLAY)
        {
            clock_mode = CLOCK_MODE_SET;
            cursor = 2;
            return;
        }

        if (clock_mode == CLOCK_MODE_SET)
        {
            if (cursor == 0)
            {
                //cursor = 2;
                clock_mode = CLOCK_MODE_DISPLAY;
                return;
            }
            cursor--;
        }
    }

    if (event == TWR_MODULE_LCD_EVENT_RIGHT_HOLD)
    {
        if (clock_mode == CLOCK_MODE_DISPLAY)
        {
            clock_mode = CLOCK_MODE_SET;
            cursor = 0;
            return;
        }
        if (clock_mode == CLOCK_MODE_SET)
        {
            if (cursor == 2)
            {
                //cursor = 0;
                clock_mode = CLOCK_MODE_DISPLAY;
                return;
            }
            cursor++;
        }
    }

    if (event == TWR_MODULE_LCD_EVENT_LEFT_CLICK)
    {
        if (clock_mode == CLOCK_MODE_SET)
        {
            if (cursor == 0)
            {
                rtc_change_time(RTC_CHANGE_HOURS, -1);
            }
            else if (cursor == 1)
            {
                rtc_change_time(RTC_CHANGE_MINUTES, -10);
            }
            else if (cursor == 2)
            {
                rtc_change_time(RTC_CHANGE_MINUTES, -1);
            }
        }

        if (clock_mode == CLOCK_MODE_DISPLAY)
        {
            display_temperature = !display_temperature;
        }
    }

    if (event == TWR_MODULE_LCD_EVENT_RIGHT_CLICK)
    {
        if (clock_mode == CLOCK_MODE_SET)
        {
            if (cursor == 0)
            {
                rtc_change_time(RTC_CHANGE_HOURS, +1);
            }
            else if (cursor == 1)
            {
                rtc_change_time(RTC_CHANGE_MINUTES, +10);
            }
            else if (cursor == 2)
            {
                rtc_change_time(RTC_CHANGE_MINUTES, +1);
            }
        }

        if (clock_mode == CLOCK_MODE_DISPLAY)
        {
            display_voltage = !display_voltage;
        }
    }
}

void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    if (event == TWR_TMP112_EVENT_UPDATE)
    {
        twr_tmp112_get_temperature_celsius(self, &temperature);
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        twr_module_battery_get_voltage(&voltage);
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(twr_lis2dh12_t *self, twr_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == TWR_LIS2DH12_EVENT_UPDATE)
    {
        twr_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (twr_lis2dh12_get_result_g(self, &result))
        {
            //twr_log_info("APP: Acceleration = [%.2f,%.2f,%.2f]", result.x_axis, result.y_axis, result.z_axis);

            // Update dice with new vectors
            twr_dice_feed_vectors(&dice, result.x_axis, result.y_axis, result.z_axis);

            // This variable holds last dice face
            static twr_dice_face_t last_face = TWR_DICE_FACE_UNKNOWN;

            // Get current dice face
            dice_face = twr_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != dice_face)
            {
                // Remember last dice face
                last_face = dice_face;

                // Convert dice face to integer
                //int orientation = dice_face;
                //twr_log_info("orientation = %d", orientation);
                if (clock_mode == CLOCK_MODE_DISPLAY && dice_face == 3)
                {
                    clock_mode = CLOCK_MODE_STOPWATCH;
                    struct tm rtc;
                    twr_rtc_get_datetime(&rtc);
                    timestamp = twr_rtc_datetime_to_timestamp(&rtc);
                }
                else if (clock_mode == CLOCK_MODE_STOPWATCH && dice_face != 3)
                {
                    clock_mode = CLOCK_MODE_DISPLAY;
                }

                twr_scheduler_plan_now(APPLICATION_TASK_ID);
            }
        }
    }
}

void application_init(void)
{
    // Initialize logging
    //twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_pulse(&led, 2000);

    // Initialize thermometer
    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    twr_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_INTERVAL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize accelerometer
    twr_lis2dh12_init(&lis2dh12, TWR_I2C_I2C0, 0x19);
    twr_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    twr_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_INTERVAL);

    twr_dice_init(&dice, TWR_DICE_FACE_UNKNOWN);

    // LCD Module
    twr_module_lcd_init();
    twr_module_lcd_set_event_handler(lcd_event_handler, NULL);
    twr_module_lcd_set_button_hold_time(400);
    pgfx = twr_module_lcd_get_gfx();
}

void application_task(void)
{
    if (!twr_gfx_display_is_ready(pgfx))
    {
        twr_scheduler_plan_current_relative(50);
        return;
    }

    if (clock_mode == CLOCK_MODE_DISPLAY)
    {
        char str[16];
        struct tm rtc;
        twr_rtc_get_datetime(&rtc);

        twr_gfx_clear(pgfx);

        if(dice_face == 2)
        {
            twr_gfx_set_rotation(pgfx, TWR_GFX_ROTATION_270);
        }
        else if (dice_face == 5)
        {
            twr_gfx_set_rotation(pgfx, TWR_GFX_ROTATION_90);
        }
        else
        {
            twr_gfx_set_rotation(pgfx, TWR_GFX_ROTATION_0);
        }

        if (display_temperature)
        {
            twr_gfx_set_font(pgfx, &twr_font_ubuntu_15);
            snprintf(str, sizeof(str), "%.1f °C", temperature);
            int w = twr_gfx_calc_string_width(pgfx, str);
            twr_gfx_draw_string(pgfx, 64 - w / 2, 15, str, 1);
        }

        if (display_voltage)
        {
            twr_gfx_set_font(pgfx, &twr_font_ubuntu_15);
            snprintf(str, sizeof(str), "%.1f V", voltage);
            int w = twr_gfx_calc_string_width(pgfx, str);
            twr_gfx_draw_string(pgfx, 64 - w / 2, 100, str, 1);
        }

        if (display_seconds)
        {
            snprintf(str, sizeof(str), "%d:%02d:%02d", rtc.tm_hour, rtc.tm_min, rtc.tm_sec);
        }
        else
        {
            snprintf(str, sizeof(str), "%d : %02d", rtc.tm_hour, rtc.tm_min);
        }

        twr_gfx_set_font(pgfx, &twr_font_ubuntu_33);
        int w = twr_gfx_calc_string_width(pgfx, str);
        twr_gfx_draw_string(pgfx, 64 - w / 2, 50, str, 1);
        twr_gfx_update(pgfx);

        if (display_seconds)
        {
            twr_scheduler_plan_current_relative(330);
        }
        else
        {
            twr_scheduler_plan_current_relative(5000);
        }
    }
    else if (clock_mode == CLOCK_MODE_SET)
    {
        // Enable PLL during mode set
        twr_system_pll_enable();

        char str[16];
        struct tm rtc;
        twr_rtc_get_datetime(&rtc);

        twr_gfx_set_font(pgfx, &twr_font_ubuntu_33);
        twr_gfx_clear(pgfx);
        twr_gfx_set_rotation(pgfx, TWR_GFX_ROTATION_0);

        snprintf(str, sizeof(str), "%d", rtc.tm_hour);
        int width_hours = twr_gfx_calc_string_width(pgfx, str);

        snprintf(str, sizeof(str), " : ");
        int width_colon = twr_gfx_calc_string_width(pgfx, str);

        snprintf(str, sizeof(str), "%d", rtc.tm_min / 10);
        int width_tenth_minutes = twr_gfx_calc_string_width(pgfx, str);

        snprintf(str, sizeof(str), "%d", rtc.tm_min % 10);
        int width_single_minutes = twr_gfx_calc_string_width(pgfx, str);

        int width_total = width_hours + width_colon + width_tenth_minutes + width_single_minutes;

        snprintf(str, sizeof(str), "%d : %02d", rtc.tm_hour, rtc.tm_min);

        int offset_left = 64 - width_total / 2;

        twr_gfx_draw_string(pgfx, offset_left, 50, str, 1);

        // Blinking
        int cursor_height = 2;
        int cursor_pos_y = 85;
        switch (cursor)
        {
            case 0:
                twr_gfx_draw_fill_rectangle(pgfx, offset_left, cursor_pos_y, offset_left + width_hours, cursor_pos_y + cursor_height, 1);
            break;

            case 1:
                twr_gfx_draw_fill_rectangle(pgfx, offset_left + width_hours + width_colon, cursor_pos_y, offset_left + width_hours + width_colon + width_tenth_minutes, cursor_pos_y + cursor_height, 1);
            break;

            case 2:
                twr_gfx_draw_fill_rectangle(pgfx, offset_left + width_hours + width_colon + width_tenth_minutes, cursor_pos_y, offset_left + width_hours + width_colon + width_tenth_minutes + width_single_minutes, cursor_pos_y + cursor_height, 1);
            break;

            default:
            break;
        }

        twr_gfx_update(pgfx);

        twr_scheduler_plan_current_relative(2000);

        // Disable PLL
        twr_system_pll_disable();
    }
    else if (clock_mode == CLOCK_MODE_STOPWATCH)
    {
        char str[16];
        struct tm rtc;
        twr_rtc_get_datetime(&rtc);

        twr_gfx_set_font(pgfx, &twr_font_ubuntu_33);
        twr_gfx_set_rotation(pgfx, TWR_GFX_ROTATION_180);

        int timestamp_diff = twr_rtc_datetime_to_timestamp(&rtc) - timestamp;

        snprintf(str, sizeof(str), "%d : %02d" , timestamp_diff / 60, timestamp_diff % 60);

        int width = twr_gfx_calc_string_width(pgfx, str);

        int offset_left = 64 - width / 2;

        twr_gfx_clear(pgfx);
        twr_gfx_draw_string(pgfx, offset_left, 50, str, 1);
        twr_gfx_update(pgfx);

        twr_scheduler_plan_current_relative(330);
    }
}
