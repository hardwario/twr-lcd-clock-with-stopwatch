#include <application.h>

// LED instance
bc_led_t led;

// Thermometer instance
bc_tmp112_t tmp112;

// Accelerometer instance
bc_lis2dh12_t lis2dh12;

// Dice instance
bc_dice_t dice;
bc_dice_face_t dice_face = BC_DICE_FACE_1;

// GFX pointer
bc_gfx_t *pgfx;

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
    bc_rtc_t rtc;
    bc_rtc_get_date_time(&rtc);

    switch (change)
    {
        case RTC_CHANGE_HOURS:
        {
            int hours_temp = rtc.hours;
            hours_temp += increment;

            if (hours_temp > 23)
            {
                hours_temp = hours_temp % 24;
            }
            else if (hours_temp < 0)
            {
                hours_temp = 24 + (hours_temp % 24);
            }

            rtc.hours = hours_temp;

            break;
        }

        case RTC_CHANGE_MINUTES:
        {
            int minutes_temp = rtc.minutes;
            minutes_temp += increment;

            if (minutes_temp > 59)
            {
                minutes_temp = minutes_temp % 60;
            }
            else if (minutes_temp < 0)
            {
                minutes_temp = 60 + (minutes_temp % 60);
            }

            rtc.minutes = minutes_temp;
            break;
        }

        default:
        {
            return;
        }
    }

    // Set year to non-zero value so RTC init code
    // would not reinitialize RTC after reset
    rtc.year = 1;

    rtc.seconds = 0;

    bc_rtc_set_date_time(&rtc);

    bc_scheduler_plan_now(APPLICATION_TASK_ID);
}

void lcd_event_handler(bc_module_lcd_event_t event, void *event_param)
{
    bc_scheduler_plan_relative(APPLICATION_TASK_ID, 10);

    if (event == BC_MODULE_LCD_EVENT_BOTH_HOLD)
    {
        display_seconds = !display_seconds;
    }

    if (event == BC_MODULE_LCD_EVENT_LEFT_HOLD)
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

    if (event == BC_MODULE_LCD_EVENT_RIGHT_HOLD)
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

    if (event == BC_MODULE_LCD_EVENT_LEFT_CLICK)
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

    if (event == BC_MODULE_LCD_EVENT_RIGHT_CLICK)
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

void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    if (event == BC_TMP112_EVENT_UPDATE)
    {
        bc_tmp112_get_temperature_celsius(self, &temperature);
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        bc_module_battery_get_voltage(&voltage);
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == BC_LIS2DH12_EVENT_UPDATE)
    {
        bc_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (bc_lis2dh12_get_result_g(self, &result))
        {
            //bc_log_info("APP: Acceleration = [%.2f,%.2f,%.2f]", result.x_axis, result.y_axis, result.z_axis);

            // Update dice with new vectors
            bc_dice_feed_vectors(&dice, result.x_axis, result.y_axis, result.z_axis);

            // This variable holds last dice face
            static bc_dice_face_t last_face = BC_DICE_FACE_UNKNOWN;

            // Get current dice face
            dice_face = bc_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != dice_face)
            {
                // Remember last dice face
                last_face = dice_face;

                // Convert dice face to integer
                //int orientation = dice_face;
                //bc_log_info("orientation = %d", orientation);
                if (clock_mode == CLOCK_MODE_DISPLAY && dice_face == 3)
                {
                    clock_mode = CLOCK_MODE_STOPWATCH;
                    bc_rtc_t rtc;
                    bc_rtc_get_date_time(&rtc);
                    timestamp = rtc.timestamp;
                }
                else if (clock_mode == CLOCK_MODE_STOPWATCH && dice_face != 3)
                {
                    clock_mode = CLOCK_MODE_DISPLAY;
                }

                bc_scheduler_plan_now(APPLICATION_TASK_ID);
            }
        }
    }
}

void application_init(void)
{
    // Initialize logging
    //bc_log_init(BC_LOG_LEVEL_DUMP, BC_LOG_TIMESTAMP_ABS);

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_pulse(&led, 2000);

    // Initialize thermometer
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_INTERVAL);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize accelerometer
    bc_lis2dh12_init(&lis2dh12, BC_I2C_I2C0, 0x19);
    bc_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    bc_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_INTERVAL);

    bc_dice_init(&dice, BC_DICE_FACE_UNKNOWN);

    // LCD Module
    bc_module_lcd_init();
    bc_module_lcd_set_event_handler(lcd_event_handler, NULL);
    bc_module_lcd_set_button_hold_time(400);
    pgfx = bc_module_lcd_get_gfx();
}

void application_task(void)
{
    if (!bc_gfx_display_is_ready(pgfx))
    {
        bc_scheduler_plan_current_relative(50);
        return;
    }

    if (clock_mode == CLOCK_MODE_DISPLAY)
    {
        char str[16];
        bc_rtc_t rtc;
        bc_rtc_get_date_time(&rtc);

        bc_gfx_clear(pgfx);

        if(dice_face == 2)
        {
            bc_gfx_set_rotation(pgfx, BC_GFX_ROTATION_270);
        }
        else if (dice_face == 5)
        {
            bc_gfx_set_rotation(pgfx, BC_GFX_ROTATION_90);
        }
        else
        {
            bc_gfx_set_rotation(pgfx, BC_GFX_ROTATION_0);
        }

        if (display_temperature)
        {
            bc_gfx_set_font(pgfx, &bc_font_ubuntu_15);
            snprintf(str, sizeof(str), "%.1f °C", temperature);
            int w = bc_gfx_calc_string_width(pgfx, str);
            bc_gfx_draw_string(pgfx, 64 - w / 2, 15, str, 1);
        }

        if (display_voltage)
        {
            bc_gfx_set_font(pgfx, &bc_font_ubuntu_15);
            snprintf(str, sizeof(str), "%.1f V", voltage);
            int w = bc_gfx_calc_string_width(pgfx, str);
            bc_gfx_draw_string(pgfx, 64 - w / 2, 100, str, 1);
        }

        if (display_seconds)
        {
            snprintf(str, sizeof(str), "%d:%02d:%02d", rtc.hours, rtc.minutes, rtc.seconds);
        }
        else
        {
            snprintf(str, sizeof(str), "%d : %02d", rtc.hours, rtc.minutes);
        }

        bc_gfx_set_font(pgfx, &bc_font_ubuntu_33);
        int w = bc_gfx_calc_string_width(pgfx, str);
        bc_gfx_draw_string(pgfx, 64 - w / 2, 50, str, 1);
        bc_gfx_update(pgfx);

        if (display_seconds)
        {
            bc_scheduler_plan_current_relative(330);
        }
        else
        {
            bc_scheduler_plan_current_relative(5000);
        }
    }
    else if (clock_mode == CLOCK_MODE_SET)
    {
        // Enable PLL during mode set
        bc_system_pll_enable();

        char str[16];
        bc_rtc_t rtc;
        bc_rtc_get_date_time(&rtc);

        bc_gfx_set_font(pgfx, &bc_font_ubuntu_33);
        bc_gfx_clear(pgfx);
        bc_gfx_set_rotation(pgfx, BC_GFX_ROTATION_0);

        snprintf(str, sizeof(str), "%d", rtc.hours);
        int width_hours = bc_gfx_calc_string_width(pgfx, str);

        snprintf(str, sizeof(str), " : ");
        int width_colon = bc_gfx_calc_string_width(pgfx, str);

        snprintf(str, sizeof(str), "%d", rtc.minutes / 10);
        int width_tenth_minutes = bc_gfx_calc_string_width(pgfx, str);

        snprintf(str, sizeof(str), "%d", rtc.minutes % 10);
        int width_single_minutes = bc_gfx_calc_string_width(pgfx, str);

        int width_total = width_hours + width_colon + width_tenth_minutes + width_single_minutes;

        snprintf(str, sizeof(str), "%d : %02d", rtc.hours, rtc.minutes);

        int offset_left = 64 - width_total / 2;

        bc_gfx_draw_string(pgfx, offset_left, 50, str, 1);

        // Blinking
        int cursor_height = 2;
        int cursor_pos_y = 85;
        switch (cursor)
        {
            case 0:
                bc_gfx_draw_fill_rectangle(pgfx, offset_left, cursor_pos_y, offset_left + width_hours, cursor_pos_y + cursor_height, 1);
            break;

            case 1:
                bc_gfx_draw_fill_rectangle(pgfx, offset_left + width_hours + width_colon, cursor_pos_y, offset_left + width_hours + width_colon + width_tenth_minutes, cursor_pos_y + cursor_height, 1);
            break;

            case 2:
                bc_gfx_draw_fill_rectangle(pgfx, offset_left + width_hours + width_colon + width_tenth_minutes, cursor_pos_y, offset_left + width_hours + width_colon + width_tenth_minutes + width_single_minutes, cursor_pos_y + cursor_height, 1);
            break;

            default:
            break;
        }

        bc_gfx_update(pgfx);

        bc_scheduler_plan_current_relative(2000);

        // Disable PLL
        bc_system_pll_disable();
    }
    else if (clock_mode == CLOCK_MODE_STOPWATCH)
    {
        char str[16];
        bc_rtc_t rtc;
        bc_rtc_get_date_time(&rtc);

        bc_gfx_set_font(pgfx, &bc_font_ubuntu_33);
        bc_gfx_set_rotation(pgfx, BC_GFX_ROTATION_180);

        int timestamp_diff = rtc.timestamp - timestamp;

        snprintf(str, sizeof(str), "%d : %02d" , timestamp_diff / 60, timestamp_diff % 60);

        int width = bc_gfx_calc_string_width(pgfx, str);

        int offset_left = 64 - width / 2;

        bc_gfx_clear(pgfx);
        bc_gfx_draw_string(pgfx, offset_left, 50, str, 1);
        bc_gfx_update(pgfx);

        bc_scheduler_plan_current_relative(330);
    }
}
